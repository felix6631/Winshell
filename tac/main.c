#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"
#include "args.h"
#include "vec.h"

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
                if (VEC_RESERVE(boundaries, capacity, count + 1) != 0) {
                    free(boundaries);
                    return -1;
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

enum { OPT_BEFORE = 1, OPT_REGEX, OPT_SEPARATOR };

static const ArgsOption options[] = {
    { OPT_BEFORE,    'b', "before",    ARGS_NONE },
    { OPT_REGEX,     'r', "regex",     ARGS_NONE },
    { OPT_SEPARATOR, 's', "separator", ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int         before;
    const char* separator;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
        case OPT_BEFORE:    o->before = 1; return 0;
        case OPT_SEPARATOR: o->separator = value; return 0;
        default:
            fprintf(stderr, PROGRAM ": regular expressions are not supported (-r)\n");
            return -1;
    }
}

int main(int argc, char* argv[]) {
    Options o = { 0, "\n" };
    const char* separator;
    const char** operands;
    size_t operandCount, separatorLength;
    int before, status = 0, count, result;

    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    operandCount = (size_t)count;
    before = o.before;
    separator = o.separator;

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
