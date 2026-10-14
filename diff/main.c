/*
 * diff: compares files line by line (normal, unified -u and context -c formats), and directories with
 * -r. Exit status: 0 the same, 1 different, 2 trouble. Lines are compared as they are, a CR before the
 * line feed included (--strip-trailing-cr ignores it), as GNU diff does.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "textutil.h"
#include "vpath.h"
#include "diff.h"

#define VERSION "0.1"
#define PROGRAM "diff"
#define EXCLUDE_MAX 64

static void print_usage(void) {
    puts("Usage: diff [OPTION]... FILES\n"
         "Compare FILES line by line: FILE1 FILE2, DIRECTORY FILE, FILE DIRECTORY, or two directories.\n"
         "\n"
         "      --normal              output a normal diff (the default)\n"
         "  -u, -U NUM, --unified[=NUM]   output NUM (default 3) lines of unified context\n"
         "  -c, -C NUM, --context[=NUM]   output NUM (default 3) lines of copied context\n"
         "  -NUM                      the number of lines of context (with -u or -c)\n"
         "      --label LABEL         use LABEL instead of the file name (twice for both files)\n"
         "  -q, --brief               report only whether the files differ\n"
         "  -s, --report-identical-files  report when two files are the same\n"
         "  -i, --ignore-case         ignore case differences in file contents\n"
         "  -w, --ignore-all-space    ignore all white space\n"
         "  -b, --ignore-space-change ignore changes in the amount of white space\n"
         "  -B, --ignore-blank-lines  ignore changes where lines are all blank\n"
         "      --strip-trailing-cr   strip a trailing CR from input lines\n"
         "  -a, --text                treat all files as text\n"
         "  -r, --recursive           compare subdirectories recursively\n"
         "  -N, --new-file            treat absent files as empty\n"
         "  -x, --exclude=PAT         exclude files and directories that match PAT\n"
         "      --help                display this help and exit\n"
         "  -v, --version             output version information and exit\n"
         "\n"
         "A FILE of - is standard input. Exit status is 0 if the inputs are the same, 1 if they\n"
         "differ, 2 if there was trouble.");
}

enum {
    OPT_NORMAL = 1, OPT_UNIFIED, OPT_CONTEXT, OPT_LABEL, OPT_BRIEF, OPT_SAME, OPT_CASE, OPT_ALL_SPACE, OPT_SPACE,
    OPT_BLANK, OPT_STRIP_CR, OPT_TEXT, OPT_RECURSIVE, OPT_NEW, OPT_EXCLUDE, OPT_ACCEPTED
};

static const ArgsOption options[] = {
    { OPT_NORMAL,    0,   "normal",                  ARGS_NONE },
    { OPT_UNIFIED,   'u', NULL,                      ARGS_NONE },
    { OPT_UNIFIED,   'U', NULL,                      ARGS_REQUIRED },
    { OPT_UNIFIED,   0,   "unified",                 ARGS_OPTIONAL },
    { OPT_CONTEXT,   'c', NULL,                      ARGS_NONE },
    { OPT_CONTEXT,   'C', NULL,                      ARGS_REQUIRED },
    { OPT_CONTEXT,   0,   "context",                 ARGS_OPTIONAL },
    { OPT_LABEL,     0,   "label",                   ARGS_REQUIRED },
    { OPT_BRIEF,     'q', "brief",                   ARGS_NONE },
    { OPT_SAME,      's', "report-identical-files",  ARGS_NONE },
    { OPT_CASE,      'i', "ignore-case",             ARGS_NONE },
    { OPT_ALL_SPACE, 'w', "ignore-all-space",        ARGS_NONE },
    { OPT_SPACE,     'b', "ignore-space-change",     ARGS_NONE },
    { OPT_BLANK,     'B', "ignore-blank-lines",      ARGS_NONE },
    { OPT_STRIP_CR,  0,   "strip-trailing-cr",       ARGS_NONE },
    { OPT_TEXT,      'a', "text",                    ARGS_NONE },
    { OPT_RECURSIVE, 'r', "recursive",               ARGS_NONE },
    { OPT_NEW,       'N', "new-file",                ARGS_NONE },
    { OPT_EXCLUDE,   'x', "exclude",                 ARGS_REQUIRED },
    { OPT_ACCEPTED,  'd', "minimal",                 ARGS_NONE },
    { OPT_ACCEPTED,  'H', "speed-large-files",       ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 2, 0, 1 };

typedef struct {
    Settings    s;
    int         brief, reportSame, recursive, newFile;
    const char* labels[2];
    int         labelCount;
    const char* excludes[EXCLUDE_MAX];
    int         excludeCount;
    char        optionText[1024];   // the options as written, for the "diff -r a b" lines
    int         status;             // 0, 1 or 2
} Run;

static int parse_context(const char* value, long* out) {
    char* end;
    long n;

    if (value == NULL)
        return 0;
    n = strtol(value, &end, 10);
    if (*value == '\0' || *end != '\0' || n < 0) {
        fprintf(stderr, "%s: invalid context length '%s'\n", PROGRAM, value);
        return -1;
    }
    *out = n;
    return 0;
}

static int on_option(int id, const char* value, void* context) {
    Run* r = context;

    switch (id) {
    case OPT_NORMAL:    r->s.format = FORMAT_NORMAL; break;
    case OPT_UNIFIED:   r->s.format = FORMAT_UNIFIED; return parse_context(value, &r->s.context);
    case OPT_CONTEXT:   r->s.format = FORMAT_CONTEXT; return parse_context(value, &r->s.context);
    case ARGS_NUMBER:   return parse_context(value, &r->s.context);      // only the length: -u or -c choose the style
    case OPT_LABEL:
        if (r->labelCount < 2)
            r->labels[r->labelCount++] = value;
        break;
    case OPT_BRIEF:     r->brief = 1; break;
    case OPT_SAME:      r->reportSame = 1; break;
    case OPT_CASE:      r->s.ignoreCase = 1; break;
    case OPT_ALL_SPACE: r->s.ignoreAllSpace = 1; break;
    case OPT_SPACE:     r->s.ignoreSpaceChange = 1; break;
    case OPT_BLANK:     r->s.ignoreBlankLines = 1; break;
    case OPT_STRIP_CR:  r->s.stripTrailingCr = 1; break;
    case OPT_TEXT:      r->s.treatAsText = 1; break;
    case OPT_RECURSIVE: r->recursive = 1; break;
    case OPT_NEW:       r->newFile = 1; break;
    case OPT_EXCLUDE:
        if (r->excludeCount < EXCLUDE_MAX)
            r->excludes[r->excludeCount++] = value;
        break;
    default:            break;
    }
    return 0;
}

/* ---- names ------------------------------------------------------------- */

static int wildcard(const char* pattern, const char* name) {
    for (; *pattern != '\0'; pattern++, name++) {
        if (*pattern == '*') {
            while (pattern[1] == '*')
                pattern++;
            for (;; name++) {
                if (wildcard(pattern + 1, name))
                    return 1;
                if (*name == '\0')
                    return 0;
            }
        }
        if (*name == '\0' || (*pattern != '?' && *pattern != *name))
            return 0;
    }
    return *name == '\0';
}

static int excluded(const Run* r, const char* name) {
    int i;

    for (i = 0; i < r->excludeCount; i++)
        if (wildcard(r->excludes[i], name))
            return 1;
    return 0;
}

static void join(const char* directory, const char* name, char* out, size_t size) {
    size_t n = strlen(directory);
    snprintf(out, size, "%s%s%s", directory, n > 0 && directory[n - 1] == '/' ? "" : "/", name);
}

static const char* base_of(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

/* ---- one pair of files ------------------------------------------------- */

enum { KIND_MISSING, KIND_FILE, KIND_DIRECTORY };

static int kind_of(const char* display) {
    char native[VPATH_MAX];
    DWORD attributes;

    if (strcmp(display, "-") == 0)
        return KIND_FILE;
    if (vpath_to_native(display, native, sizeof native) == VPATH_ROOT)
        return KIND_DIRECTORY;
    if (vpath_to_native(display, native, sizeof native) != 0 || fs_attributes(native, &attributes) != 0)
        return KIND_MISSING;
    return attributes & FILE_ATTRIBUTE_DIRECTORY ? KIND_DIRECTORY : KIND_FILE;
}

static void fail_path(Run* r, const char* display) {
    fprintf(stderr, "%s: %s: No such file or directory\n", PROGRAM, display);
    r->status = 2;
}

/** Reads a file; a missing one is empty with -N (and *missing is set). Returns 0, or -1 after a message. */
static int load(Run* r, const char* display, Text* t, FILETIME* modified, int* missing) {
    FILE* in = NULL;
    char native[VPATH_MAX];

    *missing = 0;
    memset(modified, 0, sizeof *modified);
    if (strcmp(display, "-") == 0) {
        GetSystemTimeAsFileTime(modified);
        return text_load(t, stdin, r->s.stripTrailingCr) == 0 ? 0 : -1;
    }
    if (kind_of(display) == KIND_MISSING && r->newFile) {
        memset(t, 0, sizeof *t);
        t->text = malloc(1);
        *missing = 1;
        return t->text != NULL ? 0 : -1;
    }
    if (text_open(PROGRAM, display, 1, &in) != 0)
        return -1;
    if (vpath_to_native(display, native, sizeof native) == 0) {
        HANDLE h = CreateFileA(native, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            GetFileTime(h, NULL, NULL, modified);
            CloseHandle(h);
        }
    }
    if (text_load(t, in, r->s.stripTrailingCr) != 0) {
        fclose(in);
        return -1;
    }
    fclose(in);
    return 0;
}

static void print_command(const Run* r, const char* a, const char* b) {
    printf("diff%s%s %s %s\n", r->optionText[0] != '\0' ? " " : "", r->optionText, a, b);
}

/** Compares two files; showCommand prints the "diff ... a b" line in front of a difference. */
static void compare_files(Run* r, const char* nameA, const char* nameB, int showCommand) {
    Text a, b;
    Hunk* hunks = NULL;
    size_t count = 0;
    FILETIME timeA, timeB;
    Heading first, second;
    int missingA, missingB;
    const char* shownA = r->labelCount > 0 ? r->labels[0] : nameA;
    const char* shownB = r->labelCount > 1 ? r->labels[1] : nameB;

    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    if (load(r, nameA, &a, &timeA, &missingA) != 0 || load(r, nameB, &b, &timeB, &missingB) != 0) {
        r->status = 2;
        text_free(&a);
        text_free(&b);
        return;
    }
    if ((a.binary || b.binary) && !r->s.treatAsText) {
        if (a.length != b.length || memcmp(a.text, b.text, a.length) != 0) {
            if (showCommand && !r->brief)
                print_command(r, nameA, nameB);
            printf(r->brief ? "Files %s and %s differ\n" : "Binary files %s and %s differ\n", shownA, shownB);
            if (r->status == 0)
                r->status = 1;
        } else if (r->reportSame) {
            printf("Files %s and %s are identical\n", shownA, shownB);
        }
        text_free(&a);
        text_free(&b);
        return;
    }
    if (text_prepare(&a, &b, &r->s) != 0 ||
        diff_lines(&a, &b, r->s.format == FORMAT_NORMAL ? 0 : r->s.context) != 0 ||
        diff_hunks(&a, &b, &r->s, &hunks, &count) != 0) {
        fprintf(stderr, "%s: memory exhausted\n", PROGRAM);
        r->status = 2;
    } else if (count == 0) {
        if (r->reportSame)
            printf("Files %s and %s are identical\n", shownA, shownB);
    } else {
        if (r->status == 0)
            r->status = 1;
        if (r->brief) {
            printf("Files %s and %s differ\n", shownA, shownB);
        } else {
            first.label = shownA;
            first.modified = timeA;
            second.label = shownB;
            second.modified = timeB;
            first.showTime = r->labelCount < 1;
            second.showTime = r->labelCount < 2;
            if (showCommand)
                print_command(r, nameA, nameB);
            diff_output(&r->s, &a, &b, &first, &second, hunks, count);
        }
    }
    free(hunks);
    text_free(&a);
    text_free(&b);
}

/* ---- directories ------------------------------------------------------- */

typedef struct {
    char** names;
    size_t count, capacity;
} Names;

static int compare_text(const void* x, const void* y) {
    return strcmp(*(char* const*)x, *(char* const*)y);
}

static void list_directory(const char* display, Names* out) {
    char native[VPATH_MAX], pattern[VPATH_MAX + 4];
    WIN32_FIND_DATAA data;
    HANDLE find;

    out->names = NULL;
    out->count = out->capacity = 0;
    if (vpath_to_native(display, native, sizeof native) != 0)
        return;
    snprintf(pattern, sizeof pattern, "%s\\*", native);
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        char** grown;

        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
            continue;
        if (out->count == out->capacity) {
            out->capacity = out->capacity == 0 ? 64 : out->capacity * 2;
            grown = realloc(out->names, out->capacity * sizeof *grown);
            if (grown == NULL)
                break;
            out->names = grown;
        }
        out->names[out->count++] = _strdup(data.cFileName);
    } while (FindNextFileA(find, &data));
    FindClose(find);
    qsort(out->names, out->count, sizeof *out->names, compare_text);
}

static void free_names(Names* n) {
    size_t i;

    for (i = 0; i < n->count; i++)
        free(n->names[i]);
    free(n->names);
}

static void compare_paths(Run* r, const char* a, const char* b, int top);

static void compare_entry(Run* r, const char* nameA, const char* nameB, const char* name, int inA, int inB) {
    char pathA[VPATH_MAX], pathB[VPATH_MAX];

    join(nameA, name, pathA, sizeof pathA);
    join(nameB, name, pathB, sizeof pathB);
    if (!inA && !r->newFile) {
        printf("Only in %s: %s\n", nameB, name);
        if (r->status == 0)
            r->status = 1;
        return;
    }
    if (!inB && !r->newFile) {
        printf("Only in %s: %s\n", nameA, name);
        if (r->status == 0)
            r->status = 1;
        return;
    }
    compare_paths(r, pathA, pathB, 0);
}

static void compare_directories(Run* r, const char* a, const char* b) {
    Names left, right;
    size_t i = 0, j = 0;

    list_directory(a, &left);
    list_directory(b, &right);
    while (i < left.count || j < right.count) {
        int order = i >= left.count ? 1 : (j >= right.count ? -1 : strcmp(left.names[i], right.names[j]));
        const char* name = order <= 0 ? left.names[i] : right.names[j];

        if (!excluded(r, name))
            compare_entry(r, a, b, name, order <= 0, order >= 0);
        if (order <= 0)
            i++;
        if (order >= 0)
            j++;
    }
    free_names(&left);
    free_names(&right);
}

static void compare_paths(Run* r, const char* a, const char* b, int top) {
    int kindA = kind_of(a), kindB = kind_of(b);

    if (kindA == KIND_MISSING && !r->newFile) {
        fail_path(r, a);
        return;
    }
    if (kindB == KIND_MISSING && !r->newFile) {
        fail_path(r, b);
        return;
    }
    if ((kindA == KIND_MISSING && kindB == KIND_DIRECTORY) || (kindB == KIND_MISSING && kindA == KIND_DIRECTORY)) {
        // -N: an absent directory is an empty one
        compare_directories(r, a, b);
        return;
    }
    if (kindA == KIND_DIRECTORY && kindB == KIND_DIRECTORY) {
        if (top || r->recursive) {
            compare_directories(r, a, b);
        } else {
            printf("Common subdirectories: %s and %s\n", a, b);
        }
        return;
    }
    if (kindA == KIND_DIRECTORY || kindB == KIND_DIRECTORY) {
        char inside[VPATH_MAX];

        if (top) {                              // diff DIRECTORY FILE: the file of that name in the directory
            if (kindA == KIND_DIRECTORY) {
                join(a, base_of(b), inside, sizeof inside);
                compare_files(r, inside, b, 0);
            } else {
                join(b, base_of(a), inside, sizeof inside);
                compare_files(r, a, inside, 0);
            }
            return;
        }
        printf("File %s is a %s while file %s is a %s\n", a, kindA == KIND_DIRECTORY ? "directory" : "regular file", b,
               kindB == KIND_DIRECTORY ? "directory" : "regular file");
        if (r->status == 0)
            r->status = 1;
        return;
    }
    compare_files(r, a, b, !top);
}

/** The options as written, in order, without the operands: for the line in front of each file of a directory. */
static void remember_options(Run* r, int argc, char* argv[], const char** operands, int operandCount) {
    int i, k;
    size_t used = 0;

    for (i = 1; i < argc; i++) {
        int isOperand = 0;

        for (k = 0; k < operandCount; k++)
            if (operands[k] == argv[i])
                isOperand = 1;
        if (isOperand || strcmp(argv[i], "--") == 0)
            continue;
        if (used + strlen(argv[i]) + 2 >= sizeof r->optionText)
            break;
        used += (size_t)snprintf(r->optionText + used, sizeof r->optionText - used, "%s%s", used > 0 ? " " : "", argv[i]);
    }
}

int main(int argc, char* argv[]) {
    Run run;
    const char** operands;
    int count, result;

    memset(&run, 0, sizeof run);
    run.s.context = 3;
    result = args_parse(&spec, argc, argv, on_option, &run, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count != 2) {
        fprintf(stderr, "%s: %s operand%s\n", PROGRAM, count < 2 ? "missing" : "extra",
                count < 2 ? "" : " after the second");
        args_usage_error(&spec);
        return 2;
    }
    text_binary_streams();
    remember_options(&run, argc, argv, operands, count);
    compare_paths(&run, operands[0], operands[1], 1);
    return fflush(stdout) != 0 ? 2 : run.status;
}
