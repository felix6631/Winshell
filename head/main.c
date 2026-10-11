#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"

#define VERSION "0.1"
#define PROGRAM "head"

static void print_usage(void) {
    puts("Usage: head [OPTION]... [FILE]...\n"
         "Print the first 10 lines of each FILE to standard output.\n"
         "With more than one FILE, precede each with a header giving the file name.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -c, --bytes=[-]NUM       print the first NUM bytes of each file;\n"
         "                             with the leading '-', print all but the last NUM bytes\n"
         "  -n, --lines=[-]NUM       print the first NUM lines instead of the first 10;\n"
         "                             with the leading '-', print all but the last NUM lines\n"
         "  -NUM                     the same as -n NUM\n"
         "  -q, --quiet, --silent    never print headers giving file names\n"
         "  -v, --verbose            always print headers giving file names\n"
         "      --help               display this help and exit\n"
         "      --version            output version information and exit\n"
         "\n"
         "NUM may have a multiplier suffix: b 512, k 1024, M 1024*1024, G, kB 1000, MB, GB.\n"
         "Lines end at a line feed; a CR in front of it stays with its line, so files with\n"
         "LF and with CR LF line endings are cut at the same places and are not changed.");
}

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

typedef struct {
    int       bytes;        // -c: count bytes, not lines
    long long count;        // how many
    int       fromEnd;      // -n -K / -c -K: everything but the last ones
} Mode;

/** Reads everything. The result has to be freed. */
static unsigned char* slurp(FILE* in, size_t* length) {
    size_t capacity = 65536, used = 0;
    unsigned char* data = malloc(capacity);

    while (data != NULL) {
        size_t got;
        if (used == capacity) {
            unsigned char* grown = realloc(data, capacity * 2);
            if (grown == NULL) {
                free(data);
                data = NULL;
                break;
            }
            data = grown;
            capacity *= 2;
        }
        got = fread(data + used, 1, capacity - used, in);
        if (got == 0)
            break;
        used += got;
    }
    *length = used;
    return data;
}

/** Writes the first n lines (or bytes) of the stream. Returns 0, or -1 on a write error. */
static int head_first(FILE* in, const Mode* mode) {
    unsigned char buffer[65536];
    long long remaining = mode->count;

    while (remaining > 0) {
        size_t want = sizeof buffer, got, i, take;

        if (mode->bytes && (long long)want > remaining)
            want = (size_t)remaining;
        got = fread(buffer, 1, want, in);
        if (got == 0)
            break;

        take = got;
        if (mode->bytes) {
            remaining -= (long long)got;
        } else {
            for (i = 0; i < got; i++) {
                if (buffer[i] == '\n' && --remaining == 0) {
                    take = i + 1;
                    break;
                }
            }
        }
        if (fwrite(buffer, 1, take, stdout) != take)
            return -1;
        if (take < got)
            break;
    }
    return 0;
}

/** Writes all but the last n lines (or bytes). The input is read completely first. */
static int head_all_but_last(FILE* in, const Mode* mode) {
    size_t length = 0, keep, i;
    unsigned char* data = slurp(in, &length);

    if (data == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return -1;
    }

    if (mode->bytes) {
        keep = (unsigned long long)mode->count >= length ? 0 : length - (size_t)mode->count;
    } else {
        // the number of lines; a last line without a line feed counts as well
        long long lines = 0, wanted;
        for (i = 0; i < length; i++)
            if (data[i] == '\n')
                lines++;
        if (length > 0 && data[length - 1] != '\n')
            lines++;
        wanted = lines - mode->count;

        keep = 0;
        if (wanted > 0) {
            long long seen = 0;
            keep = length;      // all of it, when the wanted lines reach the end
            for (i = 0; i < length; i++) {
                if (data[i] == '\n' && ++seen == wanted) {
                    keep = i + 1;
                    break;
                }
            }
        }
    }

    if (keep > 0 && fwrite(data, 1, keep, stdout) != keep) {
        free(data);
        return -1;
    }
    free(data);
    return 0;
}

int main(int argc, char* argv[]) {
    Mode mode = { 0, 10, 0 };
    int quiet = 0, verbose = 0, optionsEnded = 0, status = 0, files = 0, shown = 0, i;
    const char** operands = malloc((size_t)(argc + 1) * sizeof *operands);

    if (operands == NULL)
        return 1;

    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (optionsEnded || arg[0] != '-' || arg[1] == '\0') {
            operands[files++] = arg;
        } else if (strcmp(arg, "--") == 0) {
            optionsEnded = 1;
        } else if (arg[1] == '-') {
            const char* value = NULL;
            int bytes = 0, isCount = 0;

            if (strcmp(arg, "--help") == 0) {
                print_usage();
                return 0;
            } else if (strcmp(arg, "--version") == 0) {
                puts(PROGRAM " (Winshell) " VERSION);
                return 0;
            } else if (strcmp(arg, "--quiet") == 0 || strcmp(arg, "--silent") == 0) {
                quiet = 1;
                verbose = 0;
            } else if (strcmp(arg, "--verbose") == 0) {
                verbose = 1;
                quiet = 0;
            } else if (strncmp(arg, "--lines=", 8) == 0) {
                value = arg + 8;
                isCount = 1;
            } else if (strncmp(arg, "--bytes=", 8) == 0) {
                value = arg + 8;
                isCount = bytes = 1;
            } else if (strcmp(arg, "--lines") == 0 && i + 1 < argc) {
                value = argv[++i];
                isCount = 1;
            } else if (strcmp(arg, "--bytes") == 0 && i + 1 < argc) {
                value = argv[++i];
                isCount = bytes = 1;
            } else {
                fprintf(stderr, PROGRAM ": unrecognized option '%s'\n", arg);
                try_help();
                return 1;
            }

            if (isCount) {
                TextCount count;
                if (text_parse_count(value, &count) != 0) {
                    fprintf(stderr, PROGRAM ": invalid number of %s: '%s'\n", bytes ? "bytes" : "lines", value);
                    return 1;
                }
                mode.bytes = bytes;
                mode.count = count.value;
                mode.fromEnd = count.minus;
            }
        } else {
            const char* p;
            // -NUM: the digits are a line count
            if (arg[1] >= '0' && arg[1] <= '9') {
                TextCount count;
                if (text_parse_count(arg + 1, &count) != 0) {
                    fprintf(stderr, PROGRAM ": invalid number of lines: '%s'\n", arg + 1);
                    return 1;
                }
                mode.bytes = 0;
                mode.count = count.value;
                mode.fromEnd = 0;
                continue;
            }

            for (p = arg + 1; *p != '\0'; p++) {
                switch (*p) {
                    case 'q': quiet = 1; verbose = 0; break;
                    case 'v': verbose = 1; quiet = 0; break;
                    case 'n':
                    case 'c': {
                        char option = *p;
                        const char* value;
                        TextCount count;

                        if (p[1] != '\0') {
                            value = p + 1;
                        } else if (i + 1 < argc) {
                            value = argv[++i];
                        } else {
                            fprintf(stderr, PROGRAM ": option requires an argument -- '%c'\n", option);
                            try_help();
                            return 1;
                        }
                        if (text_parse_count(value, &count) != 0) {
                            fprintf(stderr, PROGRAM ": invalid number of %s: '%s'\n",
                                    option == 'c' ? "bytes" : "lines", value);
                            return 1;
                        }
                        mode.bytes = option == 'c';
                        mode.count = count.value;
                        mode.fromEnd = count.minus;
                        p += strlen(p) - 1;
                        break;
                    }
                    default:
                        fprintf(stderr, PROGRAM ": invalid option -- '%c'\n", *p);
                        try_help();
                        return 1;
                }
            }
        }
    }

    text_binary_streams();

    if (files == 0)
        operands[files++] = "-";

    for (i = 0; i < files; i++) {
        FILE* in;
        int failed;

        if (text_open(PROGRAM, operands[i], 0, &in) != 0) {
            status = 1;
            continue;
        }

        if ((files > 1 && !quiet) || verbose) {
            printf("%s==> %s <==\n", shown ? "\n" : "", strcmp(operands[i], "-") == 0 ? "standard input" : operands[i]);
            shown = 1;
        }

        failed = mode.fromEnd ? head_all_but_last(in, &mode) : head_first(in, &mode);
        if (in != stdin)
            fclose(in);
        else
            clearerr(stdin);

        if (failed != 0) {
            fprintf(stderr, PROGRAM ": write error\n");
            status = 1;
            break;
        }
    }

    free(operands);
    return status;
}
