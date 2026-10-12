#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"

#define VERSION "0.1"
#define PROGRAM "tac"

static void print_usage(void) {
    puts("Usage: tac [OPTION]... [FILE]...\n"
         "Write each FILE to standard output, last line first.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -b, --before            attach the separator before instead of after\n"
         "  -s, --separator=STRING  use STRING as the separator instead of newline\n"
         "  -r, --regex             (not supported)\n"
         "      --help              display this help and exit\n"
         "      --version           output version information and exit\n"
         "\n"
         "Lines are not changed: a CR LF ending stays with its line. A last line that\n"
         "has no ending is written first and is joined to the line that follows, as in GNU tac.");
}

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

/** Writes the records of text, last first. A record is what lies between two separators. */
static int reverse(const unsigned char* text, size_t length, const char* separator, size_t separatorLength, int before) {
    // boundaries[k] is where record k starts; there is one more than there are separators
    size_t* boundaries;
    size_t count = 0, capacity = 1024, position = 0, k;

    boundaries = malloc(capacity * sizeof *boundaries);
    if (boundaries == NULL)
        return -1;
    boundaries[count++] = 0;

    while (position + separatorLength <= length) {
        const unsigned char* found;
        size_t at;

        if (separatorLength == 1) {
            found = memchr(text + position, separator[0], length - position);
        } else {
            found = NULL;
            for (at = position; at + separatorLength <= length; at++) {
                if (text[at] == (unsigned char)separator[0] && memcmp(text + at, separator, separatorLength) == 0) {
                    found = text + at;
                    break;
                }
            }
        }
        if (found == NULL)
            break;

        at = (size_t)(found - text);
        {
            size_t boundary = before ? at : at + separatorLength;
            if (boundary > 0 && boundary < length) {
                if (count == capacity) {
                    size_t* grown = realloc(boundaries, capacity * 2 * sizeof *grown);
                    if (grown == NULL) {
                        free(boundaries);
                        return -1;
                    }
                    boundaries = grown;
                    capacity *= 2;
                }
                if (boundary > boundaries[count - 1])
                    boundaries[count++] = boundary;
            }
        }
        position = at + separatorLength;
    }

    for (k = count; k > 0; k--) {
        size_t from = boundaries[k - 1];
        size_t to = k == count ? length : boundaries[k];
        if (to > from && fwrite(text + from, 1, to - from, stdout) != to - from) {
            free(boundaries);
            return -2;
        }
    }
    free(boundaries);
    return 0;
}

int main(int argc, char* argv[]) {
    const char* separator = "\n";
    const char** operands;
    size_t operandCount = 0, separatorLength;
    int before = 0, optionsEnded = 0, status = 0, a;

    operands = calloc((size_t)argc + 1, sizeof *operands);
    if (operands == NULL) {
        fprintf(stderr, PROGRAM ": out of memory\n");
        return 1;
    }

    for (a = 1; a < argc; a++) {
        const char* arg = argv[a];

        if (optionsEnded || arg[0] != '-' || arg[1] == '\0') {
            operands[operandCount++] = arg;
        } else if (strcmp(arg, "--") == 0) {
            optionsEnded = 1;
        } else if (arg[1] == '-') {
            if (strcmp(arg, "--help") == 0) {
                print_usage();
                return 0;
            } else if (strcmp(arg, "--version") == 0) {
                puts(PROGRAM " (Winshell) " VERSION);
                return 0;
            } else if (strcmp(arg, "--before") == 0) {
                before = 1;
            } else if (strcmp(arg, "--regex") == 0) {
                fprintf(stderr, PROGRAM ": --regex is not supported\n");
                return 1;
            } else if (strncmp(arg, "--separator=", 12) == 0) {
                separator = arg + 12;
            } else if (strcmp(arg, "--separator") == 0 && a + 1 < argc) {
                separator = argv[++a];
            } else {
                fprintf(stderr, PROGRAM ": unrecognized option '%s'\n", arg);
                try_help();
                return 1;
            }
        } else {
            const char* p;
            for (p = arg + 1; *p != '\0'; p++) {
                if (*p == 'b') {
                    before = 1;
                } else if (*p == 'r') {
                    fprintf(stderr, PROGRAM ": -r is not supported\n");
                    return 1;
                } else if (*p == 's') {
                    if (p[1] != '\0') {
                        separator = p + 1;
                    } else if (a + 1 < argc) {
                        separator = argv[++a];
                    } else {
                        fprintf(stderr, PROGRAM ": option requires an argument -- 's'\n");
                        try_help();
                        return 1;
                    }
                    break;
                } else {
                    fprintf(stderr, PROGRAM ": invalid option -- '%c'\n", *p);
                    try_help();
                    return 1;
                }
            }
        }
    }

    separatorLength = strlen(separator);
    if (separatorLength == 0) {
        // as in GNU tac, an empty separator is a NUL byte
        separator = "\0";
        separatorLength = 1;
    }

    text_binary_streams();
    if (operandCount == 0)
        operands[operandCount++] = "-";

    {
        size_t i;
        for (i = 0; i < operandCount; i++) {
            FILE* in;
            unsigned char* text;
            size_t length;
            int result;

            if (text_open(PROGRAM, operands[i], 1, &in) != 0) {
                status = 1;
                continue;
            }
            text = text_slurp(in, &length);
            if (in != stdin)
                fclose(in);
            if (text == NULL) {
                fprintf(stderr, PROGRAM ": %s: cannot read\n", operands[i]);
                status = 1;
                continue;
            }
            result = reverse(text, length, separator, separatorLength, before);
            free(text);
            if (result == -2) {
                fprintf(stderr, PROGRAM ": write error\n");
                return 1;
            }
            if (result != 0) {
                fprintf(stderr, PROGRAM ": out of memory\n");
                return 1;
            }
        }
    }
    return status;
}
