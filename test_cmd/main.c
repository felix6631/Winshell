/*
 * test and [: evaluate a conditional expression; the exit status is 0 for true, 1 for false, 2 for an
 * error. One program, installed as test.exe and as [.exe (which wants a closing ]). The rules of GNU test:
 * a few arguments are read by their number (so that `test -n` and `test ! -f` mean what they should),
 * more of them as an expression with ! ( ) -a -o.
 *
 * The file tests use the virtual file system. Of the permission tests: -r is "can be opened", -w is "not
 * read-only", -x is "a directory, or a file with an extension of PATHEXT"; -u -g -k and -S are false, as there
 * is nothing like them; -O and -G are true for an existing file.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "fsutil.h"
#include "vpath.h"

#define VERSION "0.1"

typedef struct {
    const char* program;        // "test" or "["
    char* const* argv;          // the expression
    int         argc;
    int         pos;
    int         error;          // set after a message: the result is 2
} Test;

static int fail(Test* t, const char* format, const char* a, const char* b) {
    fprintf(stderr, "%s: ", t->program);
    fprintf(stderr, format, a, b);
    fputc('\n', stderr);
    t->error = 1;
    return 0;
}

/* ---- files ------------------------------------------------------------- */

typedef struct {
    int      exists;
    DWORD    attributes;
    ULONGLONG size;
    FILETIME modified, accessed;
    char     native[VPATH_MAX];
} FileInfo;

static int file_info(const char* path, FileInfo* f) {
    WIN32_FILE_ATTRIBUTE_DATA data;

    memset(f, 0, sizeof *f);
    if (path[0] == '\0')
        return 0;
    if (vpath_to_native(path, f->native, sizeof f->native) == VPATH_ROOT) {
        f->exists = 1;
        f->attributes = FILE_ATTRIBUTE_DIRECTORY;
        return 1;
    }
    if (f->native[0] == '\0' || !GetFileAttributesExA(f->native, GetFileExInfoStandard, &data))
        return 0;
    f->exists = 1;
    f->attributes = data.dwFileAttributes;
    f->size = ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    f->modified = data.ftLastWriteTime;
    f->accessed = data.ftLastAccessTime;
    return 1;
}

static int is_directory(const FileInfo* f) {
    return f->exists && (f->attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static int has_extension_of(const char* native, const char* list) {
    const char* dot = strrchr(native, '.');
    const char* slash = strrchr(native, '\\');
    char copy[256];
    char* token;
    char* next = NULL;

    if (dot == NULL || (slash != NULL && dot < slash))
        return 0;
    snprintf(copy, sizeof copy, "%s", list);
    for (token = strtok_s(copy, ";", &next); token != NULL; token = strtok_s(NULL, ";", &next))
        if (_stricmp(token, dot) == 0)
            return 1;
    return 0;
}

static int is_symlink(const FileInfo* f) {
    WIN32_FIND_DATAA data;
    HANDLE find;
    int result = 0;

    if (!f->exists || !(f->attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return 0;
    find = FindFirstFileA(f->native, &data);
    if (find != INVALID_HANDLE_VALUE) {
        result = data.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
        FindClose(find);
    }
    return result;
}

static int is_char_device(const FileInfo* f) {
    HANDLE h;
    int result = 0;

    if (!f->exists || is_directory(f))
        return 0;
    h = CreateFileA(f->native, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        result = GetFileType(h) == FILE_TYPE_CHAR;
        CloseHandle(h);
    }
    return result;
}

static int can_read(const FileInfo* f) {
    HANDLE h;

    if (!f->exists)
        return 0;
    if (is_directory(f)) {
        char pattern[VPATH_MAX + 4];
        WIN32_FIND_DATAA data;
        HANDLE find;

        if (f->native[0] == '\0')
            return 1;
        snprintf(pattern, sizeof pattern, "%s\\*", f->native);
        find = FindFirstFileA(pattern, &data);
        if (find == INVALID_HANDLE_VALUE)
            return GetLastError() == ERROR_FILE_NOT_FOUND;
        FindClose(find);
        return 1;
    }
    h = CreateFileA(f->native, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    CloseHandle(h);
    return 1;
}

static int can_execute(const FileInfo* f) {
    const char* pathext;

    if (!f->exists)
        return 0;
    if (is_directory(f))
        return 1;
    pathext = getenv("PATHEXT");
    return has_extension_of(f->native, pathext != NULL && pathext[0] != '\0' ? pathext : ".COM;.EXE;.BAT;.CMD");
}

static int unary_file_test(const char* op, const char* path, int* known) {
    FileInfo f;

    *known = 1;
    file_info(path, &f);
    switch (op[1]) {
    case 'e': return f.exists;
    case 'f': return f.exists && !is_directory(&f) && !is_char_device(&f);
    case 'd': return is_directory(&f);
    case 's': return f.exists && !is_directory(&f) && f.size > 0;
    case 'r': return can_read(&f);
    case 'w': return f.exists && !(f.attributes & FILE_ATTRIBUTE_READONLY);
    case 'x': return can_execute(&f);
    case 'L':
    case 'h': return is_symlink(&f);
    case 'c': return is_char_device(&f);
    case 'b':
    case 'p':
    case 'S':
    case 'u':
    case 'g':
    case 'k': return 0;
    case 'O':
    case 'G': return f.exists;
    case 'N': return f.exists && CompareFileTime(&f.modified, &f.accessed) > 0;
    default:
        *known = 0;
        return 0;
    }
}

/* ---- integers ---------------------------------------------------------- */

/** Reads [blanks][+-]digits[blanks]; sign and the digits without leading zeros. Returns 0 when it is not one. */
static int parse_integer(const char* text, int* negative, const char** digits, size_t* length) {
    const char* p = text;
    const char* start;

    while (isspace((unsigned char)*p))
        p++;
    *negative = 0;
    if (*p == '-' || *p == '+') {
        *negative = *p == '-';
        p++;
    }
    start = p;
    while (isdigit((unsigned char)*p))
        p++;
    if (p == start)
        return 0;
    *length = (size_t)(p - start);
    while (isspace((unsigned char)*p))
        p++;
    if (*p != '\0')
        return 0;
    while (*length > 1 && *start == '0') {
        start++;
        (*length)--;
    }
    *digits = start;
    if (*length == 1 && *start == '0')
        *negative = 0;
    return 1;
}

/** -1, 0 or 1 for a < b, a == b, a > b; numbers of any length. */
static int compare_numbers(int negA, const char* a, size_t lengthA, int negB, const char* b, size_t lengthB) {
    int magnitude;

    if (negA != negB)
        return negA ? -1 : 1;
    if (lengthA != lengthB)
        magnitude = lengthA < lengthB ? -1 : 1;
    else
        magnitude = memcmp(a, b, lengthA) < 0 ? -1 : (memcmp(a, b, lengthA) > 0 ? 1 : 0);
    return negA ? -magnitude : magnitude;
}

/* ---- the grammar ------------------------------------------------------- */

static int is_binary_operator(const char* s) {
    static const char* const operators[] = { "=", "==", "!=", "<", ">", "-eq", "-ne", "-lt", "-le", "-gt", "-ge",
                                              "-nt", "-ot", "-ef" };
    int i;

    for (i = 0; i < (int)(sizeof operators / sizeof operators[0]); i++)
        if (strcmp(s, operators[i]) == 0)
            return 1;
    return 0;
}

static int binary(Test* t, const char* a, const char* op, const char* b) {
    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0)
        return strcmp(a, b) == 0;
    if (strcmp(op, "!=") == 0)
        return strcmp(a, b) != 0;
    if (strcmp(op, "<") == 0)
        return strcmp(a, b) < 0;
    if (strcmp(op, ">") == 0)
        return strcmp(a, b) > 0;

    if (strcmp(op, "-eq") == 0 || strcmp(op, "-ne") == 0 || strcmp(op, "-lt") == 0 || strcmp(op, "-le") == 0 ||
        strcmp(op, "-gt") == 0 || strcmp(op, "-ge") == 0) {
        int negA, negB, order;
        const char *digitsA, *digitsB;
        size_t lengthA, lengthB;

        if (!parse_integer(a, &negA, &digitsA, &lengthA))
            return fail(t, "%s: integer expression expected", a, NULL);
        if (!parse_integer(b, &negB, &digitsB, &lengthB))
            return fail(t, "%s: integer expression expected", b, NULL);
        order = compare_numbers(negA, digitsA, lengthA, negB, digitsB, lengthB);
        if (strcmp(op, "-eq") == 0)
            return order == 0;
        if (strcmp(op, "-ne") == 0)
            return order != 0;
        if (strcmp(op, "-lt") == 0)
            return order < 0;
        if (strcmp(op, "-le") == 0)
            return order <= 0;
        if (strcmp(op, "-gt") == 0)
            return order > 0;
        return order >= 0;
    }

    {
        FileInfo fa, fb;

        file_info(a, &fa);
        file_info(b, &fb);
        if (strcmp(op, "-nt") == 0)
            return fa.exists && (!fb.exists || CompareFileTime(&fa.modified, &fb.modified) > 0);
        if (strcmp(op, "-ot") == 0)
            return fb.exists && (!fa.exists || CompareFileTime(&fa.modified, &fb.modified) < 0);
        if (strcmp(op, "-ef") == 0)
            return fa.exists && fb.exists && fs_same_file(fa.native, fb.native) == 1;
    }
    return 0;
}

static int expression(Test* t);
static int posixtest(Test* t, int nargs);

static const char* arg(const Test* t, int i) {
    return t->argv[i];
}

/** One term: ! term, ( expression ), a binary test, a unary test, or a string. */
static int term(Test* t) {
    int negated = 0, value;

    while (t->pos < t->argc && strcmp(arg(t, t->pos), "!") == 0) {
        t->pos++;
        negated = !negated;
    }
    if (t->pos >= t->argc)
        return fail(t, "missing argument", NULL, NULL);

    if (strcmp(arg(t, t->pos), "(") == 0) {
        int nargs;

        // what is in the parentheses: read by its number of arguments, up to four
        t->pos++;
        for (nargs = 1; t->pos + nargs < t->argc && strcmp(arg(t, t->pos + nargs), ")") != 0; nargs++) {
            if (nargs == 4) {
                nargs -= strcmp(arg(t, t->pos), "!") == 0;
                break;
            }
        }
        value = posixtest(t, nargs);
        if (t->error)
            return 0;
        if (t->pos >= t->argc)
            return fail(t, "')' expected", NULL, NULL);
        if (strcmp(arg(t, t->pos), ")") != 0)
            return fail(t, "')' expected, found %s", arg(t, t->pos), NULL);
        t->pos++;
    } else if (t->argc - t->pos >= 3 && is_binary_operator(arg(t, t->pos + 1))) {
        value = binary(t, arg(t, t->pos), arg(t, t->pos + 1), arg(t, t->pos + 2));
        t->pos += 3;
    } else if (arg(t, t->pos)[0] == '-' && arg(t, t->pos)[1] != '\0' && arg(t, t->pos)[2] == '\0' &&
               t->argc - t->pos >= 2) {
        const char* op = arg(t, t->pos);
        int known = 0;

        if (op[1] == 't') {
            int negative;
            const char* digits;
            size_t length;
            DWORD mode;

            if (!parse_integer(arg(t, t->pos + 1), &negative, &digits, &length))
                return fail(t, "invalid integer '%s'", arg(t, t->pos + 1), NULL);
            // only the three standard handles are known here; a larger number is not a terminal
            if (negative || length > 1 || digits[0] > '2') {
                value = 0;
            } else {
                int fd = digits[0] - '0';
                HANDLE h = GetStdHandle(fd == 0 ? STD_INPUT_HANDLE : (fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE));
                value = GetConsoleMode(h, &mode) != 0;
            }
            known = 1;
        } else if (op[1] == 'n') {
            value = arg(t, t->pos + 1)[0] != '\0';
            known = 1;
        } else if (op[1] == 'z') {
            value = arg(t, t->pos + 1)[0] == '\0';
            known = 1;
        } else {
            value = unary_file_test(op, arg(t, t->pos + 1), &known);
        }
        if (!known) {
            // an unknown option such as -q is a plain string when it stands alone, otherwise an error
            if (t->argc - t->pos == 1)
                value = 1;
            else
                return fail(t, "%s: unary operator expected", op, NULL);
        }
        t->pos += 2;
    } else {
        value = arg(t, t->pos)[0] != '\0';
        t->pos++;
    }
    return negated ? !value : value;
}

static int and_expression(Test* t) {
    int value = term(t);

    while (!t->error && t->pos < t->argc && strcmp(arg(t, t->pos), "-a") == 0) {
        int right;

        t->pos++;
        right = term(t);
        value = value && right;
    }
    return value;
}

static int expression(Test* t) {
    int value = and_expression(t);

    while (!t->error && t->pos < t->argc && strcmp(arg(t, t->pos), "-o") == 0) {
        int right;

        t->pos++;
        right = and_expression(t);
        value = value || right;
    }
    return value;
}

/*
 * The rules of GNU test (posixtest): with one to four arguments the meaning follows from their number, so
 * that `test -n`, `test ! -f` and `test ( x )` are what a person means; more of them are an expression.
 * Parentheses hold at most four arguments that are read the same way. Reads from t->pos; leaves t->pos after
 * the arguments it used.
 */
static int posixtest(Test* t, int nargs) {
    int p = t->pos;
    const char* a0 = p < t->argc ? arg(t, p) : "";
    int value;

    switch (nargs) {
    case 1:
        t->pos = p + 1;
        return a0[0] != '\0';

    case 2:
        if (strcmp(a0, "!") == 0) {
            t->pos = p + 2;
            return arg(t, p + 1)[0] == '\0';
        }
        if (a0[0] == '-' && a0[1] != '\0' && a0[2] == '\0')
            return term(t);
        return fail(t, "%s: unary operator expected", a0, NULL);

    case 3:
        if (is_binary_operator(arg(t, p + 1))) {
            t->pos = p + 3;
            return binary(t, a0, arg(t, p + 1), arg(t, p + 2));
        }
        if (strcmp(a0, "!") == 0) {
            t->pos = p + 1;
            value = posixtest(t, 2);
            return t->error ? 0 : !value;
        }
        if (strcmp(a0, "(") == 0 && strcmp(arg(t, p + 2), ")") == 0) {
            t->pos = p + 3;
            return arg(t, p + 1)[0] != '\0';
        }
        if (strcmp(arg(t, p + 1), "-a") == 0 || strcmp(arg(t, p + 1), "-o") == 0)
            return expression(t);
        return fail(t, "%s: binary operator expected", arg(t, p + 1), NULL);

    case 4:
        if (strcmp(a0, "!") == 0) {
            t->pos = p + 1;
            value = posixtest(t, 3);
            return t->error ? 0 : !value;
        }
        if (strcmp(a0, "(") == 0 && strcmp(arg(t, p + 3), ")") == 0) {
            t->pos = p + 1;
            value = posixtest(t, 2);
            if (!t->error)
                t->pos = p + 4;
            return value;
        }
        return expression(t);

    default:
        return expression(t);
    }
}

/** The whole expression. */
static int evaluate(Test* t) {
    int value;

    if (t->argc == 0)
        return 0;
    value = posixtest(t, t->argc);
    if (!t->error && t->pos < t->argc)
        return fail(t, "extra argument '%s'", arg(t, t->pos), NULL);
    return value;
}

static void print_usage(const char* program) {
    int bracket = strcmp(program, "[") == 0;

    printf("Usage: %s EXPRESSION\n"
           "  or:  %s\n"
           "Exit with the status determined by EXPRESSION.\n"
           "\n"
           "  ( EXPRESSION )               EXPRESSION is true\n"
           "  ! EXPRESSION                 EXPRESSION is false\n"
           "  EXPRESSION1 -a EXPRESSION2   both are true\n"
           "  EXPRESSION1 -o EXPRESSION2   either is true\n"
           "  -n STRING   the length of STRING is nonzero      -z STRING   the length is zero\n"
           "  STRING1 = STRING2, STRING1 != STRING2, STRING1 < STRING2, STRING1 > STRING2\n"
           "  INTEGER1 -eq|-ne|-lt|-le|-gt|-ge INTEGER2\n"
           "  FILE1 -nt|-ot|-ef FILE2     newer, older, the same file\n"
           "  -e -f -d -s -r -w -x -L -h -c FILE      exists, regular file, directory, size > 0, readable,\n"
           "                                          writable, executable, symbolic link, character device\n"
           "  -t FD       FD is a terminal\n"
           "\n"
           "%s"
           "Exit status: 0 if true, 1 if false, 2 if EXPRESSION is invalid.\n",
           bracket ? "[" : "test", bracket ? "[ OPTION" : "test OPTION",
           bracket ? "The last argument has to be ].\n\n" : "");
}

int main(int argc, char* argv[]) {
    Test t;
    char module[MAX_PATH];
    const char* base;
    char name[32];
    size_t length;
    int value;

    GetModuleFileNameA(NULL, module, sizeof module);
    base = strrchr(module, '\\');
    base = base != NULL ? base + 1 : module;
    snprintf(name, sizeof name, "%s", base);
    length = strlen(name);
    if (length > 4 && _stricmp(name + length - 4, ".exe") == 0)
        name[length - 4] = '\0';

    memset(&t, 0, sizeof t);
    t.program = strcmp(name, "[") == 0 ? "[" : "test";
    t.argv = argv + 1;
    t.argc = argc - 1;

    if (t.argc == 1 && strcmp(t.argv[0], "--help") == 0) {
        print_usage(t.program);
        return 0;
    }
    if (t.argc == 1 && strcmp(t.argv[0], "--version") == 0) {
        printf("%s %s\n", t.program, VERSION);
        return 0;
    }

    if (strcmp(t.program, "[") == 0) {
        if (t.argc == 0 || strcmp(argv[argc - 1], "]") != 0) {
            fprintf(stderr, "[: missing ']'\n");
            return 2;
        }
        t.argc--;
    }

    value = evaluate(&t);
    if (t.error)
        return 2;
    return value ? 0 : 1;
}
