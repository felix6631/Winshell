/*
 * realpath: the absolute, canonical form of paths of the virtual file system: "." and ".." are resolved,
 * and symbolic links and junctions are followed (unless -s). Like on Linux, by default every component
 * but the last has to exist; -e asks for all of them and -m for none.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "realpath"

static void print_usage(void) {
    puts("Usage: realpath [OPTION]... FILE...\n"
         "Print the resolved absolute file name; all but the last component must exist.\n"
         "\n"
         "  -e, --canonicalize-existing  all components of the path must exist\n"
         "  -m, --canonicalize-missing   no path components need exist or be a directory\n"
         "  -L, --logical                resolve '..' components before symlinks\n"
         "  -P, --physical               resolve symlinks as encountered (default)\n"
         "  -q, --quiet                  suppress most error messages\n"
         "      --relative-to=DIR        print the resolved path relative to DIR\n"
         "      --relative-base=DIR      print absolute paths unless paths below DIR\n"
         "  -s, --strip, --no-symlinks   don't expand symlinks\n"
         "  -z, --zero                   end each output line with NUL, not newline\n"
         "      --help                   display this help and exit\n"
         "      --version                output version information and exit\n"
         "\n"
         "Paths are paths of the virtual file system: C:\\Users\\me is /c/Users/me.");
}

enum { OPT_EXISTING = 1, OPT_MISSING, OPT_LOGICAL, OPT_PHYSICAL, OPT_QUIET, OPT_RELATIVE_TO, OPT_RELATIVE_BASE,
       OPT_STRIP, OPT_ZERO };

static const ArgsOption options[] = {
    { OPT_EXISTING,      'e', "canonicalize-existing", ARGS_NONE },
    { OPT_MISSING,       'm', "canonicalize-missing",  ARGS_NONE },
    { OPT_LOGICAL,       'L', "logical",               ARGS_NONE },
    { OPT_PHYSICAL,      'P', "physical",              ARGS_NONE },
    { OPT_QUIET,         'q', "quiet",                 ARGS_NONE },
    { OPT_RELATIVE_TO,   0,   "relative-to",           ARGS_REQUIRED },
    { OPT_RELATIVE_BASE, 0,   "relative-base",         ARGS_REQUIRED },
    { OPT_STRIP,         's', "strip",                 ARGS_NONE },
    { OPT_STRIP,         0,   "no-symlinks",           ARGS_NONE },
    { OPT_ZERO,          'z', "zero",                  ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int         existing;           /* -e */
    int         missing;            /* -m */
    int         strip;              /* -s: links are not followed */
    int         quiet;
    int         zero;
    const char* relativeTo;
    const char* relativeBase;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
        case OPT_EXISTING:      o->existing = 1; o->missing = 0; break;
        case OPT_MISSING:       o->missing = 1; o->existing = 0; break;
        case OPT_STRIP:         o->strip = 1; break;
        case OPT_QUIET:         o->quiet = 1; break;
        case OPT_ZERO:          o->zero = 1; break;
        case OPT_RELATIVE_TO:   o->relativeTo = value; break;
        case OPT_RELATIVE_BASE: o->relativeBase = value; break;
        default:                break;      /* -L and -P: the same here */
    }
    return 0;
}

/* ---- resolving ----------------------------------------------------------------------------- */

typedef enum { R_OK_, R_NOT_FOUND, R_NOT_DIRECTORY, R_TOO_LONG } Resolve;

static int is_separator(char c) {
    return c == '/' || c == '\\';
}

/** The attributes of an absolute virtual path (the root has none: it is a directory). */
static int attributes_of(const char* absolute, DWORD* attributes) {
    char native[VPATH_MAX];
    int kind = vpath_absolute_to_native(absolute, native, sizeof native);

    if (kind == VPATH_ROOT) {
        *attributes = FILE_ATTRIBUTE_DIRECTORY;
        return 0;
    }
    if (kind != 0)
        return -1;
    return fs_attributes(native, attributes);
}

/** What a link points to, as a virtual path. Returns 0, or -1 when it cannot be told (a broken link). */
static int link_target(const char* absolute, char* out, size_t size) {
    char native[VPATH_MAX], final[VPATH_MAX * 2];
    HANDLE h;
    DWORD n;
    const char* p;

    if (vpath_absolute_to_native(absolute, native, sizeof native) != 0)
        return -1;
    h = CreateFileA(native, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    n = GetFinalPathNameByHandleA(h, final, sizeof final, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(h);
    if (n == 0 || n >= sizeof final)
        return -1;
    p = final;
    if (strncmp(p, "\\\\?\\", 4) == 0)
        p += 4;
    return vpath_from_native(p, out, size);
}

/** Appends a component to an absolute virtual path. */
static int append_component(char* path, size_t size, const char* name, size_t length) {
    size_t used = strlen(path);

    if (used + length + 2 > size)
        return -1;
    if (used > 1)
        path[used++] = '/';
    memcpy(path + used, name, length);
    path[used + length] = '\0';
    return 0;
}

/** Removes the last component. */
static void remove_component(char* path) {
    size_t n = strlen(path);

    while (n > 1 && path[n - 1] != '/')
        n--;
    if (n > 1)
        n--;
    path[n] = '\0';
    if (path[0] == '\0')
        strcpy(path, "/");
}

/**
 * Walks the components of the path one by one from the start (the current directory for a relative one),
 * following links as they come. missingOk: components that do not exist are taken as they are.
 */
static Resolve resolve(const char* input, const Options* o, char* out, size_t size) {
    char start[VPATH_MAX], work[VPATH_MAX * 2];
    const char* p;
    size_t length = strlen(input);
    int trailingSeparator = length > 1 && is_separator(input[length - 1]);

    if (length == 0 || length >= VPATH_MAX)
        return length == 0 ? R_NOT_FOUND : R_TOO_LONG;

    if (is_separator(input[0]) && !(length > 1 && is_separator(input[1]) && 0)) {
        strcpy(out, "/");
    } else if (length >= 2 && input[1] == ':') {
        // C:\dir: a Windows path with a drive
        char drive[8];
        snprintf(drive, sizeof drive, "/%c", (char)tolower((unsigned char)input[0]));
        snprintf(out, size, "%s", drive);
        input += 2;
    } else {
        if (vpath_getcwd(start, sizeof start) != 0)
            strcpy(start, "/");
        snprintf(out, size, "%s", start);
    }
    snprintf(work, sizeof work, "%s", input);

    for (p = work; *p != '\0';) {
        const char* end;
        size_t n;
        int lastComponent;

        while (is_separator(*p))
            p++;
        if (*p == '\0')
            break;
        end = p;
        while (*end != '\0' && !is_separator(*end))
            end++;
        n = (size_t)(end - p);
        {
            const char* q = end;
            while (is_separator(*q))
                q++;
            lastComponent = *q == '\0';
        }

        if (n == 1 && p[0] == '.') {
            // nothing
        } else if (n == 2 && p[0] == '.' && p[1] == '.') {
            remove_component(out);
        } else {
            DWORD attributes;
            int exists;

            if (append_component(out, size, p, n) != 0)
                return R_TOO_LONG;
            exists = attributes_of(out, &attributes) == 0;

            if (!exists) {
                if (o->existing || (!o->missing && !lastComponent))
                    return R_NOT_FOUND;
            } else {
                if (!o->strip && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    char target[VPATH_MAX];
                    if (link_target(out, target, sizeof target) == 0)
                        snprintf(out, size, "%s", target);
                    if (attributes_of(out, &attributes) != 0)
                        attributes = 0;
                }
                if ((!lastComponent || trailingSeparator) && !(attributes & FILE_ATTRIBUTE_DIRECTORY) && !o->missing)
                    return R_NOT_DIRECTORY;
            }
        }
        p = end;
    }
    return R_OK_;
}

/* ---- relative paths ------------------------------------------------------------------------ */

static int same_component(const char* a, size_t an, const char* b, size_t bn) {
    return an == bn && _strnicmp(a, b, an) == 0;
}

/** Whether path is base or below it. */
static int is_below(const char* base, const char* path) {
    size_t n = strlen(base);

    if (strcmp(base, "/") == 0)
        return 1;
    return _strnicmp(path, base, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

/** path relative to base (both absolute and normalized). */
static void relative_path(const char* base, const char* path, char* out, size_t size) {
    const char* b = base;
    const char* p = path;
    size_t used = 0;

    for (;;) {
        const char *bn, *pn;
        size_t bl, pl;

        while (*b == '/')
            b++;
        while (*p == '/')
            p++;
        bn = b;
        pn = p;
        while (*bn != '\0' && *bn != '/')
            bn++;
        while (*pn != '\0' && *pn != '/')
            pn++;
        bl = (size_t)(bn - b);
        pl = (size_t)(pn - p);
        if (bl == 0 || pl == 0 || !same_component(b, bl, p, pl))
            break;
        b = bn;
        p = pn;
    }

    out[0] = '\0';
    while (*b != '\0') {
        while (*b == '/')
            b++;
        if (*b == '\0')
            break;
        while (*b != '\0' && *b != '/')
            b++;
        if (used + 4 < size) {
            memcpy(out + used, used > 0 ? "/.." : "..", used > 0 ? 3 : 2);
            used += used > 0 ? 3 : 2;
            out[used] = '\0';
        }
    }
    while (*p == '/')
        p++;
    if (*p != '\0') {
        size_t rest = strlen(p);
        if (used + rest + 2 < size) {
            if (used > 0)
                out[used++] = '/';
            memcpy(out + used, p, rest + 1);
            used += rest;
        }
    }
    if (used == 0)
        snprintf(out, size, ".");
}

static const char* describe(Resolve r) {
    switch (r) {
        case R_NOT_DIRECTORY: return "Not a directory";
        case R_TOO_LONG:      return "File name too long";
        default:              return "No such file or directory";
    }
}

int main(int argc, char* argv[]) {
    Options o;
    const char** operands;
    int count, result, i, status = 0;
    char relativeTo[VPATH_MAX], relativeBase[VPATH_MAX];
    int haveRelativeTo = 0, haveRelativeBase = 0;
    int terminator;

    memset(&o, 0, sizeof o);
    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing operand\nTry '" PROGRAM " --help' for more information.\n");
        return 1;
    }

    {
        Options dirOptions = o;
        Resolve r;

        dirOptions.existing = 0;        /* like GNU realpath: the last component may be missing */
        if (o.relativeTo != NULL) {
            r = resolve(o.relativeTo, &dirOptions, relativeTo, sizeof relativeTo);
            if (r != R_OK_) {
                if (!o.quiet)
                    fprintf(stderr, PROGRAM ": %s: %s\n", o.relativeTo, describe(r));
                return 1;
            }
            haveRelativeTo = 1;
        }
        if (o.relativeBase != NULL) {
            r = resolve(o.relativeBase, &dirOptions, relativeBase, sizeof relativeBase);
            if (r != R_OK_) {
                if (!o.quiet)
                    fprintf(stderr, PROGRAM ": %s: %s\n", o.relativeBase, describe(r));
                return 1;
            }
            haveRelativeBase = 1;
        }
    }

    _setmode(_fileno(stdout), _O_BINARY);
    terminator = o.zero ? '\0' : '\n';

    for (i = 0; i < count; i++) {
        char absolute[VPATH_MAX * 2], shown[VPATH_MAX * 2];
        Resolve r = resolve(operands[i], &o, absolute, sizeof absolute);
        const char* text = absolute;

        if (r != R_OK_) {
            if (!o.quiet)
                fprintf(stderr, PROGRAM ": %s: %s\n", operands[i], describe(r));
            status = 1;
            continue;
        }

        if (haveRelativeBase && !is_below(relativeBase, absolute)) {
            text = absolute;                    /* outside the base: absolute */
        } else if (haveRelativeTo || haveRelativeBase) {
            const char* base = haveRelativeTo ? relativeTo : relativeBase;
            if (haveRelativeTo && haveRelativeBase && !is_below(relativeBase, relativeTo)) {
                text = absolute;
            } else {
                relative_path(base, absolute, shown, sizeof shown);
                text = shown;
            }
        }
        fputs(text, stdout);
        putchar(terminator);
    }
    return fflush(stdout) != 0 ? 1 : status;
}
