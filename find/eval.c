/*
 * The walk of find and the evaluation of the expression for every file it finds.
 *
 * Paths are virtual (see common/vpath): a file is shown with the path it was found by, built from
 * the start point as typed, and opened with the Windows path that goes with it.
 */

#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmdline.h"
#include "find.h"
#include "fsutil.h"
#include "program.h"
#include "utf8.h"
#include "vec.h"
#include "vpath.h"

#define PROGRAM "find"

#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK 0xA000000C
#endif
#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT 0xA0000003
#endif

typedef struct {
    char*              display;
    char*              native;
    DWORD              attributes;
    unsigned long long size;
    FILETIME           write, access, create;
    int                isLink;
    int                isDir;
    int                depth;
    int                pruned;
    size_t             startLength;    // of the display name of the start point it was found from
} Entry;

static const FindOptions* options;
static Node* root;
static int exitStatus;
static int quitting;
static unsigned long long nowSeconds;

/* ---- file information ---------------------------------------------------------------------- */

static unsigned long long to_seconds(FILETIME t) {
    unsigned long long v = ((unsigned long long)t.dwHighDateTime << 32) | t.dwLowDateTime;
    return v / 10000000ULL - 11644473600ULL;
}

static void fill(Entry* e, const WIN32_FIND_DATAA* d) {
    e->attributes = d->dwFileAttributes;
    e->size = ((unsigned long long)d->nFileSizeHigh << 32) | d->nFileSizeLow;
    e->write = d->ftLastWriteTime;
    e->access = d->ftLastAccessTime;
    e->create = d->ftCreationTime;
    e->isLink = (d->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
                (d->dwReserved0 == IO_REPARSE_TAG_SYMLINK || d->dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT);
    e->isDir = (d->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/** What -L does for a link: the entry becomes what the link points to, when that exists. */
static void follow_link(Entry* e) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (e->isLink && GetFileAttributesExA(e->native, GetFileExInfoStandard, &data)) {
        e->attributes = data.dwFileAttributes;
        e->size = ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        e->write = data.ftLastWriteTime;
        e->access = data.ftLastAccessTime;
        e->create = data.ftCreationTime;
        e->isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e->isLink = 0;
    }
}

static void report(const char* path, const char* reason) {
    fprintf(stderr, PROGRAM ": `%s': %s\n", path, reason);
    exitStatus = 1;
}

/** Looks the start point up. Returns 0, or -1 after a message. */
static int stat_start(Entry* e, const char* path) {
    char native[VPATH_MAX];
    WIN32_FIND_DATAA data;
    HANDLE find;
    size_t length;
    int kind = vpath_to_native(path, native, sizeof native);

    memset(e, 0, sizeof *e);
    if (kind == VPATH_ROOT) {
        e->native = _strdup("");
        e->display = _strdup(path);
        e->isDir = 1;
        e->attributes = FILE_ATTRIBUTE_DIRECTORY;
        return 0;
    }
    if (kind != 0) {
        report(path, "No such file or directory");
        return -1;
    }
    e->native = _strdup(native);
    e->display = _strdup(path);

    // FindFirstFile does not take a trailing separator, and not a drive root at all
    length = strlen(native);
    while (length > 3 && (native[length - 1] == '\\' || native[length - 1] == '/'))
        native[--length] = '\0';
    find = FindFirstFileA(native, &data);
    if (find != INVALID_HANDLE_VALUE) {
        FindClose(find);
        fill(e, &data);
    } else {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!GetFileAttributesExA(native, GetFileExInfoStandard, &info)) {
            report(path, GetLastError() == ERROR_ACCESS_DENIED ? "Permission denied" : "No such file or directory");
            return -1;
        }
        e->attributes = info.dwFileAttributes;
        e->size = ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
        e->write = info.ftLastWriteTime;
        e->access = info.ftLastAccessTime;
        e->create = info.ftCreationTime;
        e->isDir = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    if (options->follow)
        follow_link(e);
    return 0;
}

/* ---- patterns ------------------------------------------------------------------------------ */

static int fold(uint32_t c, int icase) {
    return (int)(icase ? utf8_fold(c) : c);
}

/** Matches the set that starts after '[' ; returns the end of the set in *end, or 0 when it is not closed. */
static int match_set(const unsigned char* p, uint32_t c, int icase, const unsigned char** end) {
    int negate = 0, matched = 0, first = 1;
    size_t n;

    if (*p == '!' || *p == '^') {
        negate = 1;
        p++;
    }
    for (;; first = 0) {
        uint32_t lo, hi;
        if (*p == '\0')
            return -1;
        if (*p == ']' && !first)
            break;
        if (p[0] == '[' && p[1] == ':') {
            const unsigned char* close = (const unsigned char*)strstr((const char*)p + 2, ":]");
            if (close != NULL) {
                char name[16];
                size_t length = (size_t)(close - (p + 2));
                int ok = 0;
                if (length < sizeof name) {
                    memcpy(name, p + 2, length);
                    name[length] = '\0';
                    if (c < 128) {
                        if (strcmp(name, "alpha") == 0) ok = isalpha((int)c) != 0;
                        else if (strcmp(name, "digit") == 0) ok = isdigit((int)c) != 0;
                        else if (strcmp(name, "alnum") == 0) ok = isalnum((int)c) != 0;
                        else if (strcmp(name, "upper") == 0) ok = isupper((int)c) != 0;
                        else if (strcmp(name, "lower") == 0) ok = islower((int)c) != 0;
                        else if (strcmp(name, "space") == 0) ok = isspace((int)c) != 0;
                        else if (strcmp(name, "punct") == 0) ok = ispunct((int)c) != 0;
                        else if (strcmp(name, "xdigit") == 0) ok = isxdigit((int)c) != 0;
                    }
                }
                if (ok)
                    matched = 1;
                p = close + 2;
                continue;
            }
        }
        lo = utf8_decode(p, strlen((const char*)p), &n);
        p += n;
        hi = lo;
        if (p[0] == '-' && p[1] != ']' && p[1] != '\0') {
            p++;
            hi = utf8_decode(p, strlen((const char*)p), &n);
            p += n;
        }
        if ((c >= lo && c <= hi) || (icase && fold(c, 1) >= fold(lo, 1) && fold(c, 1) <= fold(hi, 1)))
            matched = 1;
    }
    *end = p + 1;
    return matched != negate;
}

static int pattern_match(const unsigned char* p, const unsigned char* s, int icase) {
    while (*p != '\0') {
        size_t n;
        if (*p == '*') {
            while (*p == '*')
                p++;
            if (*p == '\0')
                return 1;
            for (;;) {
                if (pattern_match(p, s, icase))
                    return 1;
                if (*s == '\0')
                    return 0;
                utf8_decode(s, strlen((const char*)s), &n);
                s += n;
            }
        }
        if (*s == '\0')
            return 0;
        {
            uint32_t c = utf8_decode(s, strlen((const char*)s), &n);
            if (*p == '?') {
                p++;
            } else if (*p == '[') {
                const unsigned char* end;
                int r = match_set(p + 1, c, icase, &end);
                if (r < 0) {
                    if (c != '[')
                        return 0;
                    p++;
                } else {
                    if (!r)
                        return 0;
                    p = end;
                }
            } else {
                size_t m;
                uint32_t pc;
                if (*p == '\\' && p[1] != '\0')
                    p++;
                pc = utf8_decode(p, strlen((const char*)p), &m);
                if (fold(pc, icase) != fold(c, icase))
                    return 0;
                p += m;
            }
            s += n;
        }
    }
    return *s == '\0';
}

/** The last component of the path as it was found. */
static const char* base_name(const char* path) {
    size_t length = strlen(path), end = length;
    while (end > 1 && (path[end - 1] == '/' || path[end - 1] == '\\'))
        end--;
    while (end > 0 && path[end - 1] != '/' && path[end - 1] != '\\')
        end--;
    return path + end;
}

/* ---- the tests ----------------------------------------------------------------------------- */

static int compare_number(long long value, long long wanted, int compare) {
    if (compare < 0)
        return value < wanted;
    if (compare > 0)
        return value > wanted;
    return value == wanted;
}

static int directory_is_empty(const Entry* e) {
    char pattern[VPATH_MAX + 4];
    WIN32_FIND_DATAA data;
    HANDLE find;
    int empty = 1;

    snprintf(pattern, sizeof pattern, "%s\\*", e->native);
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return 1;
    do {
        if (strcmp(data.cFileName, ".") != 0 && strcmp(data.cFileName, "..") != 0)
            empty = 0;
    } while (empty && FindNextFileA(find, &data));
    FindClose(find);
    return empty;
}

static int has_executable_extension(const char* name) {
    const char* dot = strrchr(name, '.');
    return dot != NULL && (_stricmp(dot, ".exe") == 0 || _stricmp(dot, ".bat") == 0 ||
                           _stricmp(dot, ".cmd") == 0 || _stricmp(dot, ".com") == 0);
}

static int type_matches(const char* list, const Entry* e, int resolved) {
    char type = e->isLink && !resolved ? 'l' : (e->isDir ? 'd' : 'f');
    const char* p;

    if (e->isLink && resolved) {
        // the link has no target that exists: it is a link still
        type = 'l';
    }
    for (p = list; *p != '\0'; p++)
        if (*p == type)
            return 1;
    return 0;
}

static int test_size(const Node* n, const Entry* e) {
    unsigned long long unit;
    long long count;

    switch (n->unit) {
        case 'c': unit = 1; break;
        case 'w': unit = 2; break;
        case 'k': unit = 1024; break;
        case 'M': unit = 1024ULL * 1024; break;
        case 'G': unit = 1024ULL * 1024 * 1024; break;
        default:  unit = 512; break;
    }
    count = (long long)((e->size + unit - 1) / unit);
    return compare_number(count, n->number, n->compare);
}

static FILETIME time_of(const Entry* e, char which) {
    return which == 'a' ? e->access : which == 'c' ? e->create : e->write;
}

static int test_time(const Node* n, const Entry* e) {
    long long age = (long long)nowSeconds - (long long)to_seconds(time_of(e, n->type));
    long long divisor = n->unit == 'm' ? 60 : 86400;
    // GNU find counts whole units, a time in the future is a negative number of them
    long long value = age >= 0 ? age / divisor : -((-age + divisor - 1) / divisor);
    return compare_number(value, n->number, n->compare);
}

static int run_node(Node* n, Entry* e);

/* ---- actions ------------------------------------------------------------------------------- */

static int run_command(char* const* argv, const char* directory, int confirm) {
    char path[VPATH_MAX];
    char* command;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE in = INVALID_HANDLE_VALUE;
    DWORD code = 1;
    SECURITY_ATTRIBUTES inherit = { sizeof inherit, NULL, TRUE };

    if (program_find(argv[0], path, sizeof path) != 0 &&
        SearchPathA(NULL, argv[0], ".exe", sizeof path, path, NULL) == 0) {
        fprintf(stderr, PROGRAM ": `%s': No such file or directory\n", argv[0]);
        exitStatus = 1;
        return 0;
    }
    command = cmdline_build(argv);
    if (command == NULL)
        return 0;

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    if (!confirm)
        in = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, NULL);
    si.hStdInput = in != INVALID_HANDLE_VALUE ? in : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    SetHandleInformation(si.hStdInput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(si.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(si.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    fflush(stdout);
    if (!CreateProcessA(path, command, NULL, NULL, TRUE, 0, NULL, directory, &si, &pi)) {
        fprintf(stderr, PROGRAM ": cannot run `%s'\n", argv[0]);
        exitStatus = 1;
        free(command);
        if (in != INVALID_HANDLE_VALUE)
            CloseHandle(in);
        return 0;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    free(command);
    if (in != INVALID_HANDLE_VALUE)
        CloseHandle(in);
    return code == 0;
}

/** The native directory of a path found: everything before the last separator. */
static void directory_of(const char* native, char* out, size_t size) {
    const char* slash = strrchr(native, '\\');
    size_t n = slash != NULL ? (size_t)(slash - native) : 0;
    if (n == 0 || n >= size)
        snprintf(out, size, ".");
    else {
        memcpy(out, native, n);
        out[n] = '\0';
    }
}

static char* replace_braces(const char* arg, const char* path) {
    size_t pathLength = strlen(path), count = 0, length = strlen(arg);
    const char* p;
    char* out;
    size_t o = 0;

    for (p = arg; (p = strstr(p, "{}")) != NULL; p += 2)
        count++;
    if (count == 0)
        return _strdup(arg);
    out = malloc(length + count * pathLength + 1);
    if (out == NULL)
        return NULL;
    for (p = arg; *p != '\0';) {
        if (p[0] == '{' && p[1] == '}') {
            memcpy(out + o, path, pathLength);
            o += pathLength;
            p += 2;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
    return out;
}

static void free_argv(char** argv, int count) {
    int i;
    for (i = 0; i < count; i++)
        free(argv[i]);
    free(argv);
}

/** Runs the batch of a -exec ... {} + that has collected paths. */
static void flush_batch(Node* n) {
    char** argv;
    int fixed = n->argc - 1, i;
    size_t k;
    char directory[VPATH_MAX];

    if (n->pendingCount == 0)
        return;
    argv = calloc((size_t)fixed + n->pendingCount + 1, sizeof *argv);
    if (argv == NULL)
        return;
    for (i = 0; i < fixed; i++)
        argv[i] = _strdup(n->argv[i]);
    for (k = 0; k < n->pendingCount; k++)
        argv[fixed + k] = n->pending[k];
    if (n->inDirectory && n->pendingDirectory != NULL)
        snprintf(directory, sizeof directory, "%s", n->pendingDirectory);
    if (!run_command(argv, n->inDirectory ? directory : NULL, 0))
        exitStatus = 1;
    for (i = 0; i < fixed; i++)
        free(argv[i]);
    free(argv);
    n->pendingCount = 0;
    free(n->pendingDirectory);
    n->pendingDirectory = NULL;
}

static int action_exec(Node* n, const Entry* e) {
    char path[VPATH_MAX + 4];
    char directory[VPATH_MAX];
    char** argv;
    int i, result;

    if (n->inDirectory) {
        directory_of(e->native, directory, sizeof directory);
        snprintf(path, sizeof path, "./%s", base_name(e->display));
    } else {
        snprintf(path, sizeof path, "%s", e->display);
    }

    if (n->batch) {
        if (n->inDirectory && n->pendingDirectory != NULL && strcmp(n->pendingDirectory, directory) != 0)
            flush_batch(n);
        if (VEC_RESERVE(n->pending, n->pendingCapacity, n->pendingCount + 1) != 0)
            return 0;
        n->pending[n->pendingCount++] = _strdup(path);
        if (n->inDirectory && n->pendingDirectory == NULL)
            n->pendingDirectory = _strdup(directory);
        if (n->pendingCount >= 500)
            flush_batch(n);
        return 1;
    }

    argv = calloc((size_t)n->argc + 1, sizeof *argv);
    if (argv == NULL)
        return 0;
    for (i = 0; i < n->argc; i++)
        argv[i] = replace_braces(n->argv[i], path);

    if (n->confirm) {
        char answer[16];
        fprintf(stderr, "< %s ... %s > ? ", argv[0], e->display);
        fflush(stderr);
        if (fgets(answer, sizeof answer, stdin) == NULL || (answer[0] != 'y' && answer[0] != 'Y')) {
            free_argv(argv, n->argc);
            return 0;
        }
    }
    result = run_command(argv, n->inDirectory ? directory : NULL, n->confirm);
    free_argv(argv, n->argc);
    return result;
}

static int action_delete(const Entry* e) {
    BOOL ok;

    if (e->isDir && !e->isLink) {
        ok = RemoveDirectoryA(e->native);
    } else {
        if (e->attributes & FILE_ATTRIBUTE_READONLY)
            SetFileAttributesA(e->native, e->attributes & ~FILE_ATTRIBUTE_READONLY);
        ok = DeleteFileA(e->native);
        if (!ok && e->isLink)
            ok = RemoveDirectoryA(e->native);
    }
    if (!ok) {
        fprintf(stderr, PROGRAM ": cannot delete `%s': %s\n", e->display, fs_strerror(GetLastError()));
        exitStatus = 1;
        return 0;
    }
    return 1;
}

static void print_time_seconds(FILETIME t) {
    printf("%llu.0000000000", to_seconds(t));
}

static void action_printf(const char* format, const Entry* e) {
    const char* p;

    for (p = format; *p != '\0'; p++) {
        if (*p == '\\' && p[1] != '\0') {
            p++;
            switch (*p) {
                case 'n': putchar('\n'); break;
                case 't': putchar('\t'); break;
                case 'r': putchar('\r'); break;
                case '0': putchar('\0'); break;
                case '\\': putchar('\\'); break;
                case 'a': putchar('\a'); break;
                default: putchar('\\'); putchar(*p); break;
            }
        } else if (*p == '%' && p[1] != '\0') {
            const char* base = base_name(e->display);
            p++;
            switch (*p) {
                case '%': putchar('%'); break;
                case 'p': fputs(e->display, stdout); break;
                case 'f': fputs(base, stdout); break;
                case 'h': {
                    size_t n = (size_t)(base - e->display);
                    if (n == 0)
                        fputs(".", stdout);
                    else {
                        while (n > 1 && e->display[n - 1] == '/')
                            n--;
                        fwrite(e->display, 1, n, stdout);
                    }
                    break;
                }
                case 's': printf("%llu", e->size); break;
                case 'y': putchar(e->isLink ? 'l' : e->isDir ? 'd' : 'f'); break;
                case 'P': {
                    // the path without the start point and the separator after it
                    const char* rest = e->display + e->startLength;
                    if (e->depth > 0 && *rest == '/' && (e->startLength == 0 || e->display[e->startLength - 1] != '/'))
                        rest++;
                    fputs(rest, stdout);
                    break;
                }
                case 'd': printf("%d", e->depth); break;
                case 'T':
                case 'A':
                case 'C': {
                    char which = *p;
                    if (p[1] == '@') {
                        p++;
                        print_time_seconds(which == 'A' ? e->access : which == 'C' ? e->create : e->write);
                    }
                    break;
                }
                default:
                    putchar('%');
                    putchar(*p);
                    break;
            }
        } else {
            putchar(*p);
        }
    }
}

/* ---- evaluation ---------------------------------------------------------------------------- */

static int run_test(Node* n, Entry* e) {
    switch (n->pred) {
        case P_TRUE:
        case P_OPTION:
            return 1;
        case P_FALSE:
            return 0;
        case P_NAME:
        case P_INAME:
            return pattern_match((const unsigned char*)n->text, (const unsigned char*)base_name(e->display), n->pred == P_INAME);
        case P_PATH:
        case P_IPATH:
            return pattern_match((const unsigned char*)n->text, (const unsigned char*)e->display, n->pred == P_IPATH);
        case P_REGEX: {
            RegexInput input;
            size_t s, t;
            int found;
            memset(&input, 0, sizeof input);
            if (regex_input_set(&input, (const unsigned char*)e->display, strlen(e->display)) != 0)
                return 0;
            found = regex_search(n->regex, &input, 0, &s, &t);
            regex_input_free(&input);
            return found;
        }
        case P_TYPE:
            return type_matches(n->text, e, 0);
        case P_XTYPE: {
            Entry target = *e;
            if (e->isLink)
                follow_link(&target);
            return type_matches(n->text, &target, 1);
        }
        case P_SIZE:
            return test_size(n, e);
        case P_EMPTY:
            return e->isDir ? directory_is_empty(e) : e->size == 0;
        case P_NEWER:
            return CompareFileTime(&e->write, &n->reference) > 0;
        case P_TIME:
        case P_MIN:
            return test_time(n, e);
        case P_READABLE:
            return 1;
        case P_WRITABLE:
            return !(e->attributes & FILE_ATTRIBUTE_READONLY);
        case P_EXECUTABLE:
            return e->isDir || has_executable_extension(e->display);
        case P_PRUNE:
            if (!options->depthFirst)
                e->pruned = 1;
            return 1;
        case P_QUIT:
            quitting = 1;
            return 1;
        case P_PRINT:
            fputs(e->display, stdout);
            fputc('\n', stdout);
            return 1;
        case P_PRINT0:
            fputs(e->display, stdout);
            fputc('\0', stdout);
            return 1;
        case P_PRINTF:
            action_printf(n->text, e);
            return 1;
        case P_EXEC:
            return action_exec(n, e);
        case P_DELETE:
            return action_delete(e);
    }
    return 0;
}

static int run_node(Node* n, Entry* e) {
    switch (n->kind) {
        case N_NOT:
            return !run_node(n->left, e);
        case N_AND:
            return run_node(n->left, e) && run_node(n->right, e);
        case N_OR:
            return run_node(n->left, e) || run_node(n->right, e);
        case N_COMMA:
            run_node(n->left, e);
            return run_node(n->right, e);
        default:
            return run_test(n, e);
    }
}

static void flush_all(Node* n) {
    if (n == NULL)
        return;
    flush_all(n->left);
    flush_all(n->right);
    if (n->kind == N_TEST && n->pred == P_EXEC && n->batch)
        flush_batch(n);
}

/* ---- the walk ------------------------------------------------------------------------------ */

typedef struct {
    char*           name;
    WIN32_FIND_DATAA data;
} Child;

static char* join_display(const char* directory, const char* name) {
    size_t d = strlen(directory), n = strlen(name);
    char* out = malloc(d + n + 2);
    if (out == NULL)
        return NULL;
    memcpy(out, directory, d);
    if (d > 0 && directory[d - 1] != '/' && directory[d - 1] != '\\')
        out[d++] = '/';
    memcpy(out + d, name, n + 1);
    return out;
}

static char* join_native(const char* directory, const char* name) {
    size_t d = strlen(directory), n = strlen(name);
    char* out = malloc(d + n + 2);
    if (out == NULL)
        return NULL;
    memcpy(out, directory, d);
    if (d > 0 && directory[d - 1] != '\\' && directory[d - 1] != '/')
        out[d++] = '\\';
    memcpy(out + d, name, n + 1);
    return out;
}

static void walk(Entry* e);

static void walk_children(Entry* e) {
    Child* children = NULL;
    size_t count = 0, capacity = 0, i;

    if (e->native[0] == '\0') {
        // the virtual root: the drives
        char letters[27];
        size_t n = vpath_drives(letters);
        for (i = 0; i < n; i++) {
            Child c;
            memset(&c, 0, sizeof c);
            c.name = malloc(2);
            c.name[0] = letters[i];
            c.name[1] = '\0';
            c.data.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
            if (VEC_RESERVE(children, capacity, count + 1) == 0)
                children[count++] = c;
        }
    } else {
        char pattern[VPATH_MAX + 4];
        WIN32_FIND_DATAA data;
        HANDLE find;

        snprintf(pattern, sizeof pattern, "%s%s*", e->native,
                 e->native[strlen(e->native) - 1] == '\\' ? "" : "\\");
        find = FindFirstFileA(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_FILES)
                report(e->display, error == ERROR_ACCESS_DENIED ? "Permission denied" : fs_strerror(error));
            return;
        }
        do {
            Child c;
            if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
                continue;
            c.name = _strdup(data.cFileName);
            c.data = data;
            if (c.name != NULL && VEC_RESERVE(children, capacity, count + 1) == 0)
                children[count++] = c;
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }

    for (i = 0; i < count && !quitting; i++) {
        Entry child;
        memset(&child, 0, sizeof child);
        child.display = join_display(e->display, children[i].name);
        child.native = e->native[0] == '\0'
                           ? join_native(children[i].name, "")
                           : join_native(e->native, children[i].name);
        child.depth = e->depth + 1;
        child.startLength = e->startLength;
        fill(&child, &children[i].data);
        if (e->native[0] == '\0') {
            // a drive: "c" is C:\ .
            char* root = malloc(4);
            snprintf(root, 4, "%c:\\", children[i].name[0]);
            free(child.native);
            child.native = root;
        }
        if (options->follow)
            follow_link(&child);
        walk(&child);
        free(child.display);
        free(child.native);
    }
    for (i = 0; i < count; i++)
        free(children[i].name);
    free(children);
}

static void visit(Entry* e) {
    if (e->depth >= options->minDepth)
        run_node(root, e);
}

static void walk(Entry* e) {
    if (quitting)
        return;
    if (!options->depthFirst)
        visit(e);
    if (!quitting && e->isDir && !e->pruned && (options->maxDepth < 0 || e->depth < options->maxDepth))
        walk_children(e);
    if (options->depthFirst && !quitting)
        visit(e);
}

int find_walk(Node* expression, const FindOptions* opts, const char* const* starts, int startCount) {
    int i;
    FILETIME now;

    options = opts;
    root = expression;
    GetSystemTimeAsFileTime(&now);
    nowSeconds = to_seconds(now);
    _setmode(_fileno(stdout), _O_BINARY);

    for (i = 0; i < startCount && !quitting; i++) {
        Entry e;
        if (stat_start(&e, starts[i]) != 0)
            continue;
        e.startLength = strlen(e.display);
        walk(&e);
        free(e.display);
        free(e.native);
    }
    flush_all(root);
    if (fflush(stdout) != 0)
        exitStatus = 1;
    return exitStatus;
}
