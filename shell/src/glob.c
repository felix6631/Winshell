/*
 * Wildcard expansion of the words of a command line, as a UNIX shell does it
 * before the program starts; programs never see the wildcards.
 *
 *   *        any number of characters, none included
 *   ?        any one character
 *   [abc]    one of the characters; [a-z] a range; [!abc] and [^abc] none of them
 *
 * A pattern is matched component by component against the file system, with
 * the paths of the virtual file system (/c/Users/*, ../*.txt, C:\Users\*).
 * The matches are written the way the pattern was: the part of the path
 * before the first wildcard is kept as typed.
 *
 * Differences from a UNIX shell, because of Windows:
 *  - Names are matched without regard to case, as Windows file names are.
 *  - Names starting with a dot are matched only by a pattern that starts
 *    with a dot, and files with the hidden or system attribute are not
 *    matched at all, so "rm *" does not reach system files.
 *  - The component separator may be / or \ .
 * As in UNIX, a pattern that matches nothing stays as it was typed.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <windows.h>
#include "parser.h"
#include "vpath.h"
#include "utf8.h"
#include "vec.h"

typedef struct {
    char** items;
    size_t count, capacity;
} StringList;

static int list_push(StringList* list, char* owned) {
    if (VEC_RESERVE(list->items, list->capacity, list->count + 1) != 0)
        return -1;
    list->items[list->count++] = owned;
    return 0;
}

static void list_free(StringList* list) {
    size_t i;
    for (i = 0; i < list->count; i++)
        free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = list->capacity = 0;
}

static int compare_names(const void* left, const void* right) {
    const char* a = *(const char* const*)left;
    const char* b = *(const char* const*)right;
    int result = _stricmp(a, b);
    return result != 0 ? result : strcmp(a, b);
}

/* ---- matching ------------------------------------------------------------ */

static int is_separator(char c) {
    return c == '/' || c == '\\';
}

/** Length of the pattern segment that starts at p, up to a separator or the end. */
static size_t segment_length(const char* p) {
    size_t length = 0;
    while (p[length] != '\0' && !is_separator(p[length])) {
        if (p[length] == GLOB_ESCAPE && p[length + 1] != '\0')
            length++;
        length++;
    }
    return length;
}

/** Whether a "[" at p (inside a pattern of the given end) has its closing "]". */
static const char* class_end(const char* p, const char* end) {
    const char* q = p + 1;

    if (q < end && (*q == '!' || *q == '^'))
        q++;
    if (q < end && *q == ']')       // a leading ] is part of the class
        q++;
    for (; q < end; q++) {
        if (*q == GLOB_ESCAPE && q + 1 < end)
            q++;
        else if (*q == ']')
            return q;
    }
    return NULL;
}

/** The character at *p (of which at most `left` bytes are there); *p moves past it. */
static uint32_t take_char(const char** p, size_t left) {
    size_t length;
    uint32_t c = utf8_decode((const unsigned char*)*p, left, &length);
    *p += length;
    return c;
}

/** Matches the pattern [p, pEnd) against the whole name, a character at a time, ignoring case. */
static int match(const char* p, const char* pEnd, const char* name) {
    while (p < pEnd) {
        if (*p == '*') {
            while (p < pEnd && *p == '*')
                p++;
            if (p == pEnd)
                return 1;
            for (;;) {
                if (match(p, pEnd, name))
                    return 1;
                if (*name == '\0')
                    return 0;
                take_char(&name, 4);            // whole characters only
            }
        } else if (*p == '?') {
            if (*name == '\0')
                return 0;
            p++;
            take_char(&name, 4);
        } else if (*p == '[' && class_end(p, pEnd) != NULL) {
            const char* end = class_end(p, pEnd);
            const char* q = p + 1;
            int negate = 0, found = 0;
            uint32_t c;

            if (*name == '\0')
                return 0;
            c = utf8_fold(take_char(&name, 4));
            if (*q == '!' || *q == '^') {
                negate = 1;
                q++;
            }
            while (q < end) {
                uint32_t low, high;
                if (*q == GLOB_ESCAPE && q + 1 < end)
                    q++;
                low = utf8_fold(take_char(&q, (size_t)(end - q)));
                high = low;
                if (q + 1 < end && *q == '-') {
                    q++;
                    if (*q == GLOB_ESCAPE && q + 1 < end)
                        q++;
                    high = utf8_fold(take_char(&q, (size_t)(end - q)));
                }
                if (c >= low && c <= high)
                    found = 1;
            }
            if (found == negate)
                return 0;
            p = end + 1;
        } else {
            uint32_t want, have;
            if (*p == GLOB_ESCAPE && p + 1 < pEnd)
                p++;
            if (*name == '\0')
                return 0;
            want = take_char(&p, (size_t)(pEnd - p));
            have = take_char(&name, 4);
            if (utf8_fold(want) != utf8_fold(have))
                return 0;
        }
    }
    return *name == '\0';
}

static int segment_has_wildcard(const char* p, size_t length) {
    const char* end = p + length;
    const char* q;
    for (q = p; q < end; q++) {
        if (*q == GLOB_ESCAPE) {
            q++;
        } else if (*q == '*' || *q == '?') {
            return 1;
        } else if (*q == '[' && class_end(q, end) != NULL) {
            return 1;
        }
    }
    return 0;
}

/* ---- walking the file system ----------------------------------------------- */

typedef struct {
    StringList results;
    int failed;             // out of memory
} Context;

static char* join(const char* a, size_t aLength, const char* b, size_t bLength, const char* c, size_t cLength) {
    char* out = malloc(aLength + bLength + cLength + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, a, aLength);
    memcpy(out + aLength, b, bLength);
    memcpy(out + aLength + bLength, c, cLength);
    out[aLength + bLength + cLength] = '\0';
    return out;
}

/** The segment's text without the escapes. The result has to be freed. */
static char* unescape(const char* p, size_t length) {
    char* out = malloc(length + 1);
    size_t i, n = 0;
    if (out == NULL)
        return NULL;
    for (i = 0; i < length; i++) {
        if (p[i] == GLOB_ESCAPE && i + 1 < length)
            i++;
        out[n++] = p[i];
    }
    out[n] = '\0';
    return out;
}

static int path_exists(const char* path) {
    char native[VPATH_MAX];
    int kind = vpath_to_native(path, native, sizeof native);
    if (kind == VPATH_ROOT)
        return 1;
    return kind == 0 && GetFileAttributesA(native) != INVALID_FILE_ATTRIBUTES;
}

typedef struct {
    char* name;
    int isDirectory;
} DirEntry;

/**
 * Names in the directory (a path of the virtual file system; "" is the
 * current one) that the segment pattern matches. Returns 0, or -1 for out of memory.
 */
static int matching_names(const char* directory, const char* segment, size_t length, int needDirectory,
                          StringList* names) {
    char native[VPATH_MAX], pattern[VPATH_MAX + 4];
    int kind = vpath_to_native(directory[0] == '\0' ? "." : directory, native, sizeof native);
    int dotAllowed = segment[0] == '.' || (segment[0] == GLOB_ESCAPE && segment[1] == '.');
    const char* end = segment + length;

    if (kind == VPATH_ROOT) {
        // the drives
        char letters[27];
        size_t count = vpath_drives(letters), i;
        for (i = 0; i < count; i++) {
            char name[2];
            name[0] = letters[i];
            name[1] = '\0';
            if (match(segment, end, name)) {
                char* copy = _strdup(name);
                if (copy == NULL || list_push(names, copy) != 0)
                    return -1;
            }
        }
        return 0;
    }

    if (kind == 0) {
        WIN32_FIND_DATAA data;
        HANDLE find;
        size_t n = strlen(native);
        int needSeparator = n > 0 && !is_separator(native[n - 1]);

        snprintf(pattern, sizeof pattern, "%s%s*", native, needSeparator ? "\\" : "");
        find = FindFirstFileA(pattern, &data);
        if (find == INVALID_HANDLE_VALUE)
            return 0;
        do {
            if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
                continue;
            if (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
                continue;
            if (data.cFileName[0] == '.' && !dotAllowed)
                continue;
            if (needDirectory && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            if (match(segment, end, data.cFileName)) {
                char* copy = _strdup(data.cFileName);
                if (copy == NULL || list_push(names, copy) != 0) {
                    FindClose(find);
                    return -1;
                }
            }
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }
    return 0;
}

/**
 * Expands the pattern from position p. prefix holds what was written so far,
 * path and separator included.
 */
static void walk(Context* context, const char* prefix, const char* p);

/** Goes on with the rest of the pattern below the directory candidate (which is taken over). */
static void descend(Context* context, char* candidate, char separator, const char* rest) {
    char sep[2];
    char* next;

    sep[0] = separator;
    sep[1] = '\0';
    next = join(candidate, strlen(candidate), sep, 1, "", 0);
    free(candidate);
    if (next == NULL) {
        context->failed = 1;
        return;
    }
    walk(context, next, rest);
    free(next);
}

/** A segment without wildcards: the one name, which has to exist when it is the last. */
static void walk_literal(Context* context, const char* prefix, const char* p, size_t length, char separator) {
    char* literal = unescape(p, length);
    char* candidate;

    if (literal == NULL) {
        context->failed = 1;
        return;
    }
    candidate = join(prefix, strlen(prefix), literal, strlen(literal), "", 0);
    free(literal);
    if (candidate == NULL) {
        context->failed = 1;
        return;
    }

    if (separator == '\0') {
        if (path_exists(candidate) && list_push(&context->results, candidate) == 0)
            return;
        free(candidate);
        return;
    }
    descend(context, candidate, separator, p + length + 1);
}

/** A segment with wildcards: every name of the directory that matches, in order. */
static void walk_wildcard(Context* context, const char* prefix, const char* p, size_t length, char separator) {
    StringList names = { NULL, 0, 0 };
    int last = separator == '\0';
    size_t prefixLength = strlen(prefix), i;

    if (matching_names(prefix, p, length, !last, &names) != 0) {
        list_free(&names);
        context->failed = 1;
        return;
    }
    qsort(names.items, names.count, sizeof *names.items, compare_names);

    for (i = 0; i < names.count && !context->failed; i++) {
        char* candidate = join(prefix, prefixLength, names.items[i], strlen(names.items[i]), "", 0);

        if (candidate == NULL) {
            context->failed = 1;
            break;
        }
        if (last) {
            if (list_push(&context->results, candidate) != 0) {
                free(candidate);
                context->failed = 1;
            }
        } else {
            descend(context, candidate, separator, p + length + 1);
        }
    }
    list_free(&names);
}

static void walk(Context* context, const char* prefix, const char* p) {
    size_t length = segment_length(p);
    char separator = p[length];         // the character after the segment, or '\0' at the end

    if (context->failed)
        return;
    if (segment_has_wildcard(p, length))
        walk_wildcard(context, prefix, p, length, separator);
    else
        walk_literal(context, prefix, p, length, separator);
}

/**
 * Expands one pattern. Returns 0 and the sorted matches (the list may be
 * empty), or -1 for out of memory.
 */
static int expand_pattern(const char* pattern, StringList* results) {
    Context context;

    memset(&context, 0, sizeof context);
    walk(&context, "", pattern);
    if (context.failed) {
        list_free(&context.results);
        return -1;
    }
    if (context.results.count > 1)
        qsort(context.results.items, context.results.count, sizeof *context.results.items, compare_names);
    *results = context.results;
    return 0;
}

/* ---- the pipeline ---------------------------------------------------------------- */

static int owned_push(Pipeline* pipeline, char* string) {
    if (VEC_RESERVE(pipeline->owned, pipeline->ownedCapacity, pipeline->ownedCount + 1) != 0)
        return -1;
    pipeline->owned[pipeline->ownedCount++] = string;
    return 0;
}

int winshell_glob(Pipeline* pipeline) {
    size_t c;

    for (c = 0; c < pipeline->count; c++) {
        Command* command = &pipeline->commands[c];
        StringList words = { NULL, 0, 0 };     // the new argv; its strings are not owned by the list
        size_t i;
        int any = 0;

        if (command->patterns == NULL)
            continue;
        for (i = 0; command->argv[i] != NULL; i++)
            if (command->patterns[i] != NULL)
                any = 1;
        if (!any)
            continue;

        for (i = 0; command->argv[i] != NULL; i++) {
            if (command->patterns[i] != NULL) {
                StringList matches = { NULL, 0, 0 };
                size_t m;

                if (expand_pattern(command->patterns[i], &matches) != 0)
                    goto err_memory;

                if (matches.count == 0) {
                    // like UNIX: no match, the word stays
                    if (list_push(&words, command->argv[i]) != 0)
                        goto err_memory;
                } else {
                    for (m = 0; m < matches.count; m++) {
                        if (owned_push(pipeline, matches.items[m]) != 0 ||
                            list_push(&words, matches.items[m]) != 0)
                            goto err_memory;
                        matches.items[m] = NULL;       // now owned by the pipeline
                    }
                }
                list_free(&matches);
            } else if (list_push(&words, command->argv[i]) != 0) {
                goto err_memory;
            }
        }

        // the new argv, NULL-terminated; patterns are no longer needed
        if (list_push(&words, NULL) != 0)
            goto err_memory;
        free(command->argv);
        free(command->patterns);
        command->argv = words.items;
        command->patterns = NULL;
    }
    return 0;

err_memory:
    fprintf(stderr, "Error: Out of memory\n");
    return -1;
}
