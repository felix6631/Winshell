/*
 * pathchk: diagnoses file names that are invalid or unportable, as GNU pathchk does. By default the length of the
 * name and of each component (255 and 4095, as on Linux) and that the directories on the way are directories;
 * -p the POSIX limits (14 and 255) and the portable characters; -P an empty name and a leading "-".
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
#define PROGRAM "pathchk"

static void print_usage(void) {
    puts("Usage: pathchk [OPTION]... NAME...\n"
         "Diagnose invalid or unportable file names.\n"
         "\n"
         "  -p                  check for most POSIX systems\n"
         "  -P                  check for empty names and leading \"-\"\n"
         "      --portability   check for all POSIX systems (equivalent to -p -P)\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit");
}

enum { OPT_BASIC = 1, OPT_EXTRA, OPT_PORTABILITY };

static const ArgsOption options[] = {
    { OPT_BASIC,       'p', NULL,          ARGS_NONE },
    { OPT_EXTRA,       'P', NULL,          ARGS_NONE },
    { OPT_PORTABILITY, 0,   "portability", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct { int basic, extra; } Mode;

static int on_option(int id, const char* value, void* context) {
    Mode* m = context;

    (void)value;
    if (id == OPT_BASIC || id == OPT_PORTABILITY)
        m->basic = 1;
    if (id == OPT_EXTRA || id == OPT_PORTABILITY)
        m->extra = 1;
    return 0;
}

#define NAME_MAX_LIMIT 255
#define PATH_MAX_LIMIT 4096
#define POSIX_NAME_MAX 14
#define POSIX_PATH_MAX 256

/** A name as the messages show it: plain when it is plain, else in quotes, unprintable bytes as $'\ooo'. */
static void print_quoted_as(FILE* out, const char* name, int always) {
    const unsigned char* p;
    int plain = *name != '\0' && !always;
    int printable = 1;

    for (p = (const unsigned char*)name; *p != '\0'; p++) {
        if (*p < 32 || *p >= 127)
            printable = 0;
        if (!(isalnum(*p) || strchr("_./+,:@%=-", *p) != NULL))
            plain = 0;
    }
    if (plain) {
        fputs(name, out);
        return;
    }
    if (printable) {
        fputc('\'', out);
        for (p = (const unsigned char*)name; *p != '\0'; p++) {
            if (*p == '\'')
                fputs("'\\''", out);
            else
                fputc(*p, out);
        }
        fputc('\'', out);
        return;
    }
    // runs of printable bytes in single quotes, the others as $'..'
    {
        int inQuote = 0, inEscape = 0;

        for (p = (const unsigned char*)name; ; p++) {
            int special = *p != '\0' && (*p < 32 || *p >= 127);

            if (*p == '\0') {
                if (inQuote || inEscape)
                    fputc('\'', out);
                else if (name[0] == '\0')
                    fputs("''", out);
                break;
            }
            if (special) {
                if (!inEscape) {
                    if (inQuote)
                        fputc('\'', out);
                    else if (p == (const unsigned char*)name)
                        fputs("''", out);
                    fputs("$'", out);
                    inEscape = 1;
                    inQuote = 0;
                }
                switch (*p) {
                case '\t': fputs("\\t", out); break;
                case '\n': fputs("\\n", out); break;
                case '\r': fputs("\\r", out); break;
                default: fprintf(out, "\\%03o", *p);
                }
            } else {
                if (!inQuote) {
                    if (inEscape)
                        fputc('\'', out);
                    fputc('\'', out);
                    inQuote = 1;
                    inEscape = 0;
                }
                if (*p == '\'')
                    fputs("'\\''", out);
                else
                    fputc(*p, out);
            }
        }
    }
}

/** The name as a message shows it inside the text (quote): always in quotes. */
static void print_quoted(FILE* out, const char* name) {
    print_quoted_as(out, name, 1);
}

static void message(const char* first, const char* name, const char* last) {
    fprintf(stderr, PROGRAM ": %s", first);
    print_quoted_as(stderr, name, 0);
    fputs(last, stderr);
}

/** The character as `%c` in the message: printable as it is, others as \t, \n or \ooo, between quotes. */
static void char_message(unsigned char c, const char* name) {
    fprintf(stderr, PROGRAM ": nonportable character '");
    switch (c) {
    case '\t': fputs("\\t", stderr); break;
    case '\n': fputs("\\n", stderr); break;
    case '\r': fputs("\\r", stderr); break;
    default:
        if (c < 32 || c >= 127)
            fprintf(stderr, "\\%03o", c);
        else
            fputc(c, stderr);
    }
    fputs("' in file name ", stderr);
    print_quoted(stderr, name);
    fputc('\n', stderr);
}

static int is_portable(unsigned char c) {
    return isalnum(c) || c == '.' || c == '_' || c == '-' || c == '/';
}

static int validate(const char* file, const Mode* mode) {
    size_t length = strlen(file), nameMax, pathMax, pos;

    if (*file == '\0') {
        if (mode->basic || mode->extra) {
            fprintf(stderr, PROGRAM ": empty file name\n");
        } else {
            message("", file, ": No such file or directory\n");
        }
        return 0;
    }
    if (mode->basic) {
        const char* p;

        for (p = file; *p != '\0'; p++) {
            if (!is_portable((unsigned char)*p)) {
                char_message((unsigned char)*p, file);
                return 0;
            }
        }
        nameMax = POSIX_NAME_MAX;
        pathMax = POSIX_PATH_MAX;
    } else {
        nameMax = NAME_MAX_LIMIT;
        pathMax = PATH_MAX_LIMIT;
    }
    if (length >= pathMax) {
        fprintf(stderr, PROGRAM ": limit %u exceeded by length %u of file name ", (unsigned)(pathMax - 1), (unsigned)length);
        print_quoted(stderr, file);
        fputc('\n', stderr);
        return 0;
    }

    // the components: the directories on the way have to be directories, each name has to fit
    {
        int exists = 1;                         // while the prefixes exist they are looked at
        pos = 0;
        while (file[pos] != '\0') {
            size_t start = pos, end;
            char prefix[VPATH_MAX];

            while (file[pos] == '/')
                pos++;
            start = pos;
            while (file[pos] != '\0' && file[pos] != '/')
                pos++;
            end = pos;
            if (end == start)
                break;
            if (mode->extra && file[start] == '-') {
                fprintf(stderr, PROGRAM ": leading '-' in a component of file name ");
                print_quoted(stderr, file);
                fputc('\n', stderr);
                return 0;
            }
            if (end - start > nameMax) {
                fprintf(stderr, PROGRAM ": limit %u exceeded by length %u of file name component ", (unsigned)nameMax,
                        (unsigned)(end - start));
                snprintf(prefix, sizeof prefix, "%.*s", (int)(end - start), file + start);
                print_quoted(stderr, prefix);
                fputc('\n', stderr);
                return 0;
            }
            if (!mode->basic && exists && file[pos] == '/') {
                // a directory on the way: what exists there has to be a directory
                char native[VPATH_MAX];
                DWORD attributes;

                snprintf(prefix, sizeof prefix, "%.*s", (int)end, file);
                if (vpath_to_native(prefix, native, sizeof native) == 0 && fs_attributes(native, &attributes) == 0) {
                    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        message("", file, ": Not a directory\n");
                        return 0;
                    }
                } else {
                    exists = 0;
                }
            }
        }
    }
    return 1;
}

int main(int argc, char* argv[]) {
    Mode mode;
    const char** operands;
    int count, result, i, ok = 1;

    memset(&mode, 0, sizeof mode);
    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, &mode, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        args_usage_error(&spec);
        return 1;
    }
    for (i = 0; i < count; i++)
        ok &= validate(operands[i], &mode);
    return ok ? 0 : 1;
}
