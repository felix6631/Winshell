/*
 * stdbuf: runs a command with other buffering of its standard streams. GNU stdbuf does it by loading a library into the
 * command that changes the buffering of the C library (LD_PRELOAD); Windows has no such way, so the options are checked as
 * GNU stdbuf checks them (the mode L or 0 or a size such as 10K, 1MiB, line buffering is meaningless for the input, the
 * status 125 for an error of stdbuf) and the command is run as it is. The buffering of the command is its own.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "exec.h"

#define VERSION "0.1"
#define PROGRAM "stdbuf"

static void print_usage(void) {
    puts("Usage: stdbuf OPTION... COMMAND\n"
         "Run COMMAND, with modified buffering operations for its standard streams.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  -i, --input=MODE   adjust standard input stream buffering\n"
         "  -o, --output=MODE  adjust standard output stream buffering\n"
         "  -e, --error=MODE   adjust standard error stream buffering\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "If MODE is 'L' the corresponding stream will be line buffered.\n"
         "This option is invalid with standard input.\n"
         "\n"
         "If MODE is '0' the corresponding stream will be unbuffered.\n"
         "\n"
         "Otherwise MODE is a number which may be followed by one of the following:\n"
         "KB 1000, K 1024, MB 1000*1000, M 1024*1024, and so on for G, T, P, E, Z, Y.\n"
         "Binary prefixes can be used, too: KiB=K, MiB=M, and so on.\n"
         "In this case the corresponding stream will be fully buffered with the buffer\n"
         "size set to MODE bytes.\n"
         "\n"
         "NOTE: Windows has no way to change the buffering of another program: the modes are checked and\n"
         "COMMAND is run unchanged.");
}

static const ArgsOption options[] = {
    { 'i', 'i', "input",  ARGS_REQUIRED },
    { 'o', 'o', "output", ARGS_REQUIRED },
    { 'e', 'e', "error",  ARGS_REQUIRED },
    { 0 }
};

/* the status of an error of stdbuf is 125, and the first operand ends the options */
static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 125, 1, 0 };

typedef struct {
    int given;
} Stdbuf;

/** The text in quotes the way GNU quote() writes it: 'text', a quote inside it as '\''. */
static const char* quoted(const char* text, char* out, size_t size) {
    size_t used = 0;

    out[used++] = '\'';
    for (; *text != '\0' && used + 6 < size; text++) {
        if (*text == '\'') {
            memcpy(out + used, "'\\''", 4);
            used += 4;
        } else {
            out[used++] = *text;
        }
    }
    out[used++] = '\'';
    out[used] = '\0';
    return out;
}

/**
 * Checks a MODE: L, or a size (a number and perhaps KB, K, KiB, MB, M, MiB ... up to Y). Returns 0, or -1 after printing the
 * message. A size too large gives "Value too large for defined data type".
 */
static int check_mode(char option, const char* mode) {
    const char* p = mode;
    unsigned long long value = 0;
    int overflow = 0;
    int digits = 0;
    char shown[512];

    if (strcmp(mode, "L") == 0) {
        if (option == 'i') {
            fprintf(stderr, PROGRAM ": line buffering stdin is meaningless\n");
            args_usage_error(&spec);
            return -1;
        }
        return 0;
    }
    while (isspace((unsigned char)*p))
        p++;
    while (isdigit((unsigned char)*p)) {
        unsigned digit = (unsigned)(*p - '0');

        if (value > (ULLONG_MAX - digit) / 10)
            overflow = 1;
        else
            value = value * 10 + digit;
        p++;
        digits++;
    }
    if (digits > 0) {
        /* the suffix: [KMGTPEZY] with an optional iB or B (KiB = K = 1024, KB = 1000) */
        static const char letters[] = "KMGTPEZY";
        int ok = 0;

        if (*p == '\0') {
            ok = 1;
        } else if (strchr(letters, *p == 'k' ? 'K' : *p) != NULL && *p != '\0') {
            int power = (int)(strchr(letters, *p == 'k' ? 'K' : *p) - letters) + 1;
            unsigned long long factor = 1;
            const char* rest = p + 1;
            int i, base = 1024;

            if (strcmp(rest, "") == 0) {
                ok = 1;
            } else if (strcmp(rest, "iB") == 0) {
                ok = 1;
            } else if (strcmp(rest, "B") == 0) {
                ok = 1;
                base = 1000;
            }
            for (i = 0; ok && i < power; i++) {
                if (factor > ULLONG_MAX / (unsigned)base) {
                    overflow = 1;
                    break;
                }
                factor *= (unsigned)base;
            }
            if (ok && !overflow && value != 0 && factor > ULLONG_MAX / value)
                overflow = 1;
        } else if (strcmp(p, "b") == 0) {
            ok = 0;
        }
        if (ok) {
            if (overflow) {
                fprintf(stderr, PROGRAM ": invalid mode %s: Value too large for defined data type\n", quoted(mode, shown, sizeof shown));
                return -1;
            }
            return 0;
        }
    }
    fprintf(stderr, PROGRAM ": invalid mode %s\n", quoted(mode, shown, sizeof shown));
    return -1;
}

static int on_option(int id, const char* value, void* context) {
    Stdbuf* s = (Stdbuf*)context;

    s->given = 1;
    return check_mode((char)id, value) == 0 ? 0 : -1;
}

int main(int argc, char* argv[]) {
    Stdbuf s;
    const char** operands;
    int count, result;

    memset(&s, 0, sizeof s);
    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, &s, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (!s.given) {
        if (count == 0) {
            fprintf(stderr, PROGRAM ": missing operand\n");
        } else {
            fprintf(stderr, PROGRAM ": you must specify a buffering mode option\n");
        }
        args_usage_error(&spec);
        return 125;
    }
    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        args_usage_error(&spec);
        return 125;
    }
    fflush(stdout);
    return exec_run(PROGRAM, 1, (char* const*)operands, 0, GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE),
                    GetStdHandle(STD_ERROR_HANDLE));
}
