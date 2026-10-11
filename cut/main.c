#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"

#define VERSION "0.1"
#define PROGRAM "cut"

static void print_usage(void) {
    puts("Usage: cut OPTION... [FILE]...\n"
         "Print selected parts of lines from each FILE to standard output.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -b, --bytes=LIST        select only these bytes\n"
         "  -c, --characters=LIST   select only these characters (UTF-8)\n"
         "  -d, --delimiter=DELIM   use DELIM instead of TAB for field delimiter\n"
         "  -f, --fields=LIST       select only these fields; also print any line\n"
         "                            that contains no delimiter character, unless\n"
         "                            the -s option is specified\n"
         "  -n                      (ignored)\n"
         "      --complement        complement the set of selected bytes, characters or fields\n"
         "  -s, --only-delimited    do not print lines not containing delimiters\n"
         "      --output-delimiter=STRING  use STRING as the output delimiter\n"
         "                            the default is to use the input delimiter\n"
         "      --help              display this help and exit\n"
         "      --version           output version information and exit\n"
         "\n"
         "Use one, and only one of -b, -c or -f. Each LIST is made up of one range, or\n"
         "many ranges separated by commas: N, N-M, N- (from N to the end), -M (from the\n"
         "first to M). Counting starts at 1.\n"
         "A CR in front of the line feed is part of the line ending: it is never in the\n"
         "last field, and it is written back at the end of the line.");
}

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

typedef struct {
    size_t from, to;        // 1 based, inclusive; to == (size_t)-1 means to the end
} Range;

static Range* ranges;
static size_t rangeCount;

static int parse_list(const char* list) {
    const char* p = list;

    if (*p == '\0') {
        fprintf(stderr, PROGRAM ": fields are numbered from 1\n");
        return -1;
    }
    while (*p != '\0') {
        Range range;
        char* end;
        int hasFrom = 0;

        range.from = 1;
        range.to = (size_t)-1;

        if (*p >= '0' && *p <= '9') {
            range.from = (size_t)strtoul(p, &end, 10);
            p = end;
            hasFrom = 1;
            range.to = range.from;
        }
        if (*p == '-') {
            p++;
            if (*p >= '0' && *p <= '9') {
                range.to = (size_t)strtoul(p, &end, 10);
                p = end;
            } else {
                range.to = (size_t)-1;
                if (!hasFrom) {
                    fprintf(stderr, PROGRAM ": invalid range with no endpoint: -\n");
                    return -1;
                }
            }
        } else if (!hasFrom) {
            fprintf(stderr, PROGRAM ": invalid byte, character or field list\n");
            return -1;
        }

        if (range.from == 0 && hasFrom) {
            fprintf(stderr, PROGRAM ": fields are numbered from 1\n");
            return -1;
        }
        if (range.from == 0)
            range.from = 1;
        if (range.to != (size_t)-1 && range.to < range.from) {
            fprintf(stderr, PROGRAM ": invalid decreasing range\n");
            return -1;
        }

        {
            Range* grown = realloc(ranges, (rangeCount + 1) * sizeof *ranges);
            if (grown == NULL)
                return -1;
            ranges = grown;
            ranges[rangeCount++] = range;
        }

        if (*p == ',') {
            p++;
            if (*p == '\0') {
                fprintf(stderr, PROGRAM ": invalid byte, character or field list\n");
                return -1;
            }
        } else if (*p != '\0') {
            fprintf(stderr, PROGRAM ": invalid byte, character or field list\n");
            return -1;
        }
    }
    return 0;
}

static int complement_option;

/** Whether the item with this number (from 1) is selected. */
static int selected(size_t number) {
    size_t i;
    int in = 0;
    for (i = 0; i < rangeCount; i++) {
        if (number >= ranges[i].from && (ranges[i].to == (size_t)-1 || number <= ranges[i].to)) {
            in = 1;
            break;
        }
    }
    return complement_option ? !in : in;
}

/** The index of the first range of the list that holds the number, or -1. */
static long range_of(size_t number) {
    size_t i;
    for (i = 0; i < rangeCount; i++)
        if (number >= ranges[i].from && (ranges[i].to == (size_t)-1 || number <= ranges[i].to))
            return (long)i;
    return -1;
}

static int is_continuation(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

static int cut_stream(FILE* in, int mode, int delimiter, int onlyDelimited, const char* outputDelimiter,
                      size_t delimiterLength) {
    size_t length = 0, offset = 0;
    unsigned char* text = text_slurp(in, &length);
    TextLine line;

    if (text == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return -1;
    }

    while (text_next_line(text, length, &offset, &line)) {
        size_t i;

        if (mode == 'f') {
            int hasDelimiter = memchr(line.content, delimiter, line.length) != NULL;
            size_t fieldStart = 0, field = 1;
            int first = 1;

            if (!hasDelimiter) {
                if (!onlyDelimited)
                    text_write_line(&line, stdout);
                continue;
            }

            for (i = 0; i <= line.length; i++) {
                if (i == line.length || line.content[i] == (unsigned char)delimiter) {
                    if (selected(field)) {
                        if (!first)
                            fwrite(outputDelimiter, 1, delimiterLength, stdout);
                        fwrite(line.content + fieldStart, 1, i - fieldStart, stdout);
                        first = 0;
                    }
                    field++;
                    fieldStart = i + 1;
                }
            }
            // the line ending
            fputs(line.ending == 2 ? "\r\n" : "\n", stdout);
            continue;
        }

        {
            // bytes or characters: write the selected ones, in the order of the line
            size_t number = 0;
            int first = 1, previousSelected = 0;
            long previousRange = -1;

            for (i = 0; i < line.length; i++) {
                size_t start = i, end = i + 1;

                if (mode == 'c') {
                    while (end < line.length && is_continuation(line.content[end]))
                        end++;
                    i = end - 1;
                }
                number++;

                if (selected(number)) {
                    // with an output delimiter, one is written between the ranges of the list
                    long range = complement_option ? 0 : range_of(number);
                    int newRange = complement_option ? !previousSelected : range != previousRange;

                    if (!first && newRange && delimiterLength > 0)
                        fwrite(outputDelimiter, 1, delimiterLength, stdout);
                    fwrite(line.content + start, 1, end - start, stdout);
                    first = 0;
                    previousSelected = 1;
                    previousRange = range;
                } else {
                    previousSelected = 0;
                }
            }
            fputs(line.ending == 2 ? "\r\n" : "\n", stdout);
        }
    }

    free(text);
    return 0;
}

int main(int argc, char* argv[]) {
    int mode = 0, delimiter = '\t', onlyDelimited = 0, optionsEnded = 0, operandCount = 0, status = 0, i;
    const char* outputDelimiter = NULL;
    size_t outputDelimiterLength = 0;
    const char** operands = malloc((size_t)(argc + 1) * sizeof *operands);
    const char* list = NULL;
    char single[2];

    if (operands == NULL)
        return 1;

    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (optionsEnded || arg[0] != '-' || arg[1] == '\0') {
            operands[operandCount++] = arg;
        } else if (strcmp(arg, "--") == 0) {
            optionsEnded = 1;
        } else if (arg[1] == '-') {
            const char* value = NULL;
            int newMode = 0;

            if (strcmp(arg, "--help") == 0) {
                print_usage();
                return 0;
            } else if (strcmp(arg, "--version") == 0) {
                puts(PROGRAM " (Winshell) " VERSION);
                return 0;
            } else if (strcmp(arg, "--complement") == 0) {
                complement_option = 1;
            } else if (strcmp(arg, "--only-delimited") == 0) {
                onlyDelimited = 1;
            } else if (strncmp(arg, "--bytes=", 8) == 0) {
                value = arg + 8;
                newMode = 'b';
            } else if (strncmp(arg, "--characters=", 13) == 0) {
                value = arg + 13;
                newMode = 'c';
            } else if (strncmp(arg, "--fields=", 9) == 0) {
                value = arg + 9;
                newMode = 'f';
            } else if (strncmp(arg, "--delimiter=", 12) == 0) {
                value = arg + 12;
                if (value[0] == '\0') {
                    delimiter = '\0';
                } else if (value[1] != '\0') {
                    fprintf(stderr, PROGRAM ": the delimiter must be a single character\n");
                    try_help();
                    return 1;
                } else {
                    delimiter = (unsigned char)value[0];
                }
                value = NULL;
            } else if (strncmp(arg, "--output-delimiter=", 19) == 0) {
                outputDelimiter = arg + 19;
            } else {
                fprintf(stderr, PROGRAM ": unrecognized option '%s'\n", arg);
                try_help();
                return 1;
            }

            if (newMode != 0) {
                if (mode != 0 && mode != newMode) {
                    fprintf(stderr, PROGRAM ": only one type of list may be specified\n");
                    try_help();
                    return 1;
                }
                mode = newMode;
                list = value;
            }
        } else {
            const char* p;
            for (p = arg + 1; *p != '\0'; p++) {
                switch (*p) {
                    case 'n': break;
                    case 's': onlyDelimited = 1; break;
                    case 'b':
                    case 'c':
                    case 'f':
                    case 'd': {
                        char option = *p;
                        const char* value;

                        if (p[1] != '\0') {
                            value = p + 1;
                        } else if (i + 1 < argc) {
                            value = argv[++i];
                        } else {
                            fprintf(stderr, PROGRAM ": option requires an argument -- '%c'\n", option);
                            try_help();
                            return 1;
                        }

                        if (option == 'd') {
                            if (value[0] == '\0') {
                                delimiter = '\0';
                            } else if (value[1] != '\0') {
                                fprintf(stderr, PROGRAM ": the delimiter must be a single character\n");
                                try_help();
                                return 1;
                            } else {
                                delimiter = (unsigned char)value[0];
                            }
                        } else {
                            if (mode != 0 && mode != option) {
                                fprintf(stderr, PROGRAM ": only one type of list may be specified\n");
                                try_help();
                                return 1;
                            }
                            mode = option;
                            list = value;
                        }
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

    if (mode == 0) {
        fprintf(stderr, PROGRAM ": you must specify a list of bytes, characters, or fields\n");
        try_help();
        return 1;
    }
    if (onlyDelimited && mode != 'f') {
        fprintf(stderr, PROGRAM ": suppressing non-delimited lines makes sense\n"
                        "\tonly when operating on fields\n");
        try_help();
        return 1;
    }
    if (parse_list(list) != 0)
        return 1;

    if (outputDelimiter == NULL) {
        single[0] = (char)delimiter;
        single[1] = '\0';
        outputDelimiter = mode == 'f' ? single : "";
        outputDelimiterLength = mode == 'f' ? 1 : 0;
    } else {
        // an empty string is a NUL byte, as with GNU cut
        outputDelimiterLength = outputDelimiter[0] == '\0' ? 1 : strlen(outputDelimiter);
    }

    text_binary_streams();
    if (operandCount == 0)
        operands[operandCount++] = "-";

    for (i = 0; i < operandCount; i++) {
        FILE* in;
        if (text_open(PROGRAM, operands[i], 1, &in) != 0) {
            status = 1;
            continue;
        }
        if (cut_stream(in, mode, delimiter, onlyDelimited, outputDelimiter, outputDelimiterLength) != 0)
            status = 1;
        if (in != stdin)
            fclose(in);
        else
            clearerr(stdin);
    }

    fflush(stdout);
    free(operands);
    return status;
}
