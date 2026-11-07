/*
 * mktemp: makes a temporary file or directory with a name that has not been used, and prints the name. The
 * template ends with at least three X, which are replaced by random letters and digits.
 */

#define _CRT_RAND_S
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
#define PROGRAM "mktemp"

static void print_usage(void) {
    puts("Usage: mktemp [OPTION]... [TEMPLATE]\n"
         "Create a temporary file or directory, safely, and print its name.\n"
         "TEMPLATE must contain at least 3 consecutive 'X's in last component.\n"
         "If TEMPLATE is not specified, use tmp.XXXXXXXXXX, and --tmpdir is implied.\n"
         "Files are created u+rw, and directories u+rwx, minus umask restrictions.\n"
         "\n"
         "  -d, --directory     create a directory, not a file\n"
         "  -u, --dry-run       do not create anything; merely print a name (unsafe)\n"
         "  -q, --quiet         suppress diagnostics about file/dir-creation failure\n"
         "      --suffix=SUFF   append SUFF to TEMPLATE; SUFF must not contain a slash.\n"
         "                        This option is implied if TEMPLATE does not end in X\n"
         "  -p DIR, --tmpdir[=DIR]  interpret TEMPLATE relative to DIR; if DIR is not\n"
         "                        specified, use $TMPDIR if set, else the temporary directory;\n"
         "                        in any case, TEMPLATE must not be an absolute name\n"
         "  -t                  interpret TEMPLATE as a single file name component,\n"
         "                        relative to a directory: $TMPDIR, if set; else the\n"
         "                        directory specified via -p; else the temporary directory\n"
         "      --help          display this help and exit\n"
         "      --version       output version information and exit");
}

enum { OPT_DIRECTORY = 1, OPT_DRY_RUN, OPT_QUIET, OPT_SUFFIX, OPT_TMPDIR, OPT_T };

static const ArgsOption options[] = {
    { OPT_DIRECTORY, 'd', "directory", ARGS_NONE },
    { OPT_DRY_RUN,   'u', "dry-run",   ARGS_NONE },
    { OPT_QUIET,     'q', "quiet",     ARGS_NONE },
    { OPT_SUFFIX,    0,   "suffix",    ARGS_REQUIRED },
    { OPT_TMPDIR,    'p', NULL,        ARGS_REQUIRED },
    { OPT_TMPDIR,    0,   "tmpdir",    ARGS_OPTIONAL },
    { OPT_T,         't', NULL,        ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int         directory, dryRun, quiet, useTmpdir, t;
    const char* suffix;
    const char* tmpdir;
} Settings;

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;

    switch (id) {
        case OPT_DIRECTORY: s->directory = 1; break;
        case OPT_DRY_RUN:   s->dryRun = 1; break;
        case OPT_QUIET:     s->quiet = 1; break;
        case OPT_SUFFIX:    s->suffix = value; break;
        case OPT_TMPDIR:    s->useTmpdir = 1; if (value != NULL) s->tmpdir = value; break;
        case OPT_T:         s->t = 1; s->useTmpdir = 1; break;
    }
    return 0;
}

/** The directory for the temporary files: $TMPDIR, else the temporary directory of Windows, as a virtual path. */
static void default_directory(char* out, size_t size) {
    const char* env = getenv("TMPDIR");
    char native[MAX_PATH];

    if (env != NULL && env[0] != '\0') {
        snprintf(out, size, "%s", env);
        return;
    }
    if (GetTempPathA(sizeof native, native) > 0) {
        size_t n = strlen(native);

        while (n > 3 && (native[n - 1] == '\\' || native[n - 1] == '/'))
            native[--n] = '\0';
        if (vpath_from_native(native, out, size) == 0)
            return;
    }
    snprintf(out, size, "/tmp");
}

static void fill_random(char* x, size_t count) {
    static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t i;

    for (i = 0; i < count; i++) {
        unsigned int value = 0;

        if (rand_s(&value) != 0)
            value = (unsigned int)GetTickCount() * 2654435761u + (unsigned int)i;
        x[i] = letters[value % (sizeof letters - 1)];
    }
}

int main(int argc, char* argv[]) {
    Settings s;
    const char** operands;
    int count, result, attempts;
    char directoryText[VPATH_MAX];
    char name[VPATH_MAX * 2];
    char native[VPATH_MAX * 2];
    const char* templateText;
    const char* suffix;
    size_t length, xCount = 0, suffixLength;
    char* xStart;

    _setmode(_fileno(stdout), _O_BINARY);
    memset(&s, 0, sizeof s);
    result = args_parse(&spec, argc, argv, on_option, &s, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 1) {
        fprintf(stderr, PROGRAM ": too many templates\n");
        args_usage_error(&spec);
        return 1;
    }

    templateText = count == 1 ? operands[0] : "tmp.XXXXXXXXXX";
    suffix = s.suffix != NULL ? s.suffix : "";
    if (count == 0)
        s.useTmpdir = 1;
    if (strchr(suffix, '/') != NULL || strchr(suffix, '\\') != NULL) {
        fprintf(stderr, PROGRAM ": invalid suffix '%s', contains directory separator\n", suffix);
        return 1;
    }
    if (s.t && (strchr(templateText, '/') != NULL || strchr(templateText, '\\') != NULL)) {
        fprintf(stderr, PROGRAM ": invalid template, '%s', contains directory separator\n", templateText);
        return 1;
    }

    // the name: [directory/]template+suffix
    if (s.useTmpdir) {
        const char* dir = s.tmpdir;

        if (s.t) {
            const char* env = getenv("TMPDIR");

            if (env != NULL && env[0] != '\0')
                dir = env;
        }
        if (dir == NULL) {
            default_directory(directoryText, sizeof directoryText);
            dir = directoryText;
        }
        if (vpath_is_separator(templateText[0]) || (isalpha((unsigned char)templateText[0]) && templateText[1] == ':')) {
            fprintf(stderr, PROGRAM ": invalid template, '%s'; with --tmpdir, it may not be absolute\n", templateText);
            return 1;
        }
        length = strlen(dir);
        snprintf(name, sizeof name, "%s%s%s%s", dir, length > 0 && vpath_is_separator(dir[length - 1]) ? "" : "/", templateText, suffix);
    } else {
        snprintf(name, sizeof name, "%s%s", templateText, suffix);
    }

    // the X's end the template: just before the suffix
    length = strlen(name);
    suffixLength = strlen(suffix);
    xStart = name + length - suffixLength;
    while (xStart > name && xStart[-1] == 'X') {
        xStart--;
        xCount++;
    }
    if (xCount < 3) {
        fprintf(stderr, PROGRAM ": too few X's in template '%s'\n", templateText);
        return 1;
    }

    for (attempts = 0; attempts < 1000; attempts++) {
        int kind;

        fill_random(xStart, xCount);
        if (s.dryRun) {
            printf("%s\n", name);
            return fflush(stdout) != 0 ? 1 : 0;
        }
        kind = vpath_to_native(name, native, sizeof native);
        if (kind != 0) {
            if (!s.quiet)
                fprintf(stderr, PROGRAM ": failed to create %s via template '%s': Invalid argument\n",
                        s.directory ? "directory" : "file", templateText);
            return 1;
        }
        if (s.directory) {
            if (CreateDirectoryA(native, NULL))
                break;
        } else {
            HANDLE file = CreateFileA(native, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

            if (file != INVALID_HANDLE_VALUE) {
                CloseHandle(file);
                break;
            }
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS && GetLastError() != ERROR_FILE_EXISTS) {
            if (!s.quiet)
                fprintf(stderr, PROGRAM ": failed to create %s via template '%s': %s\n", s.directory ? "directory" : "file",
                        templateText, fs_strerror(GetLastError()));
            return 1;
        }
    }
    if (attempts == 1000) {
        if (!s.quiet)
            fprintf(stderr, PROGRAM ": failed to create %s via template '%s': File exists\n",
                    s.directory ? "directory" : "file", templateText);
        return 1;
    }
    printf("%s\n", name);
    return fflush(stdout) != 0 ? 1 : 0;
}
