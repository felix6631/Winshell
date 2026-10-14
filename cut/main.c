#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"
#include "args.h"
#include "vec.h"

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

/** The list of ranges that -b, -c or -f gave, and whether the rest is wanted instead (--complement). */
typedef struct {
    Range* ranges;
    size_t rangeCount;
    int    complement;
} Selection;

static int parse_list(Selection* selection, const char* list) {
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

        if (VEC_GROW_BY_COUNT(selection->ranges, selection->rangeCount) != 0)
            return -1;
        selection->ranges[selection->rangeCount++] = range;

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

/** Whether the item with this number (from 1) is selected. */
static int selected(const Selection* selection, size_t number) {
    size_t i;
    int in = 0;
    for (i = 0; i < selection->rangeCount; i++) {
        const Range* r = &selection->ranges[i];
        if (number >= r->from && (r->to == (size_t)-1 || number <= r->to)) {
            in = 1;
            break;
        }
    }
    return selection->complement ? !in : in;
}

/** The index of the first range of the list that holds the number, or -1. */
static long range_of(const Selection* selection, size_t number) {
    size_t i;
    for (i = 0; i < selection->rangeCount; i++) {
        const Range* r = &selection->ranges[i];
        if (number >= r->from && (r->to == (size_t)-1 || number <= r->to))
            return (long)i;
    }
    return -1;
}

static int is_continuation(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

static int cut_stream(const Selection* selection, FILE* in, int mode, int delimiter, int onlyDelimited,
                      const char* outputDelimiter, size_t delimiterLength) {
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
                    if (selected(selection, field)) {
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

                if (selected(selection, number)) {
                    // with an output delimiter, one is written between the ranges of the list
                    long range = selection->complement ? 0 : range_of(selection, number);
                    int newRange = selection->complement ? !previousSelected : range != previousRange;

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

enum { OPT_BYTES = 1, OPT_CHARACTERS, OPT_FIELDS, OPT_DELIMITER, OPT_IGNORED, OPT_ONLY_DELIMITED,
       OPT_COMPLEMENT, OPT_OUTPUT_DELIMITER };

static const ArgsOption options[] = {
    { OPT_BYTES,            'b', "bytes",            ARGS_REQUIRED },
    { OPT_CHARACTERS,       'c', "characters",       ARGS_REQUIRED },
    { OPT_FIELDS,           'f', "fields",           ARGS_REQUIRED },
    { OPT_DELIMITER,        'd', "delimiter",        ARGS_REQUIRED },
    { OPT_IGNORED,          'n', NULL,               ARGS_NONE },
    { OPT_ONLY_DELIMITED,   's', "only-delimited",   ARGS_NONE },
    { OPT_COMPLEMENT,       0,   "complement",       ARGS_NONE },
    { OPT_OUTPUT_DELIMITER, 0,   "output-delimiter", ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    Selection   selection;
    int         mode;               // 'b', 'c' or 'f'
    const char* list;
    int         delimiter;
    int         onlyDelimited;
    const char* outputDelimiter;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;
    int newMode = 0;

    switch (id) {
        case OPT_BYTES:      newMode = 'b'; break;
        case OPT_CHARACTERS: newMode = 'c'; break;
        case OPT_FIELDS:     newMode = 'f'; break;
        case OPT_DELIMITER:
            if (value[0] == '\0') {
                o->delimiter = '\0';
            } else if (value[1] != '\0') {
                fprintf(stderr, PROGRAM ": the delimiter must be a single character\n");
                try_help();
                return -1;
            } else {
                o->delimiter = (unsigned char)value[0];
            }
            return 0;
        case OPT_ONLY_DELIMITED:   o->onlyDelimited = 1; return 0;
        case OPT_COMPLEMENT:       o->selection.complement = 1; return 0;
        case OPT_OUTPUT_DELIMITER: o->outputDelimiter = value; return 0;
        default:                   return 0;
    }

    if (o->mode != 0 && o->mode != newMode) {
        fprintf(stderr, PROGRAM ": only one type of list may be specified\n");
        try_help();
        return -1;
    }
    o->mode = newMode;
    o->list = value;
    return 0;
}

int main(int argc, char* argv[]) {
    Options o;
    int mode, delimiter, onlyDelimited, operandCount, status = 0, i, result;
    const char* outputDelimiter;
    size_t outputDelimiterLength = 0;
    const char** operands;
    const char* list;
    char single[2];

    memset(&o, 0, sizeof o);
    o.delimiter = '\t';

    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &operandCount);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    mode = o.mode;
    list = o.list;
    delimiter = o.delimiter;
    onlyDelimited = o.onlyDelimited;
    outputDelimiter = o.outputDelimiter;

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
    if (parse_list(&o.selection, list) != 0)
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
        if (cut_stream(&o.selection, in, mode, delimiter, onlyDelimited, outputDelimiter, outputDelimiterLength) != 0)
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
