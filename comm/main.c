/*
 * comm: compares two sorted files line by line and writes three columns: the lines only in FILE1, only in
 * FILE2, and in both. Lines are compared as bytes (the C locale); a CR in front of the line feed is not a
 * part of the line, so files with LF and with CR LF compare alike, and a line is written as it was read.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "textutil.h"

#define VERSION "0.1"
#define PROGRAM "comm"

static void print_usage(void) {
    puts("Usage: comm [OPTION]... FILE1 FILE2\n"
         "Compare sorted files FILE1 and FILE2 line by line.\n"
         "\n"
         "With no options, produce three-column output. Column one contains lines unique to FILE1,\n"
         "column two contains lines unique to FILE2, and column three contains lines common to both.\n"
         "\n"
         "  -1                      suppress column 1 (lines unique to FILE1)\n"
         "  -2                      suppress column 2 (lines unique to FILE2)\n"
         "  -3                      suppress column 3 (lines that appear in both files)\n"
         "      --check-order       check that the input is correctly sorted, even if all input lines are pairable\n"
         "      --nocheck-order     do not check that the input is correctly sorted\n"
         "      --output-delimiter=STR  separate columns with STR\n"
         "      --total             output a summary\n"
         "  -z, --zero-terminated   line delimiter is NUL, not newline\n"
         "      --help              display this help and exit\n"
         "      --version           output version information and exit\n"
         "\n"
         "FILE1 or FILE2 (not both) can be -, meaning standard input.\n"
         "Exit status: 0, 1 if a file cannot be read or the input is not sorted, 2 for a wrong option.");
}

enum { OPT_1 = 1, OPT_2, OPT_3, OPT_CHECK, OPT_NOCHECK, OPT_DELIMITER, OPT_TOTAL, OPT_ZERO };

static const ArgsOption options[] = {
    { OPT_1,         '1', NULL,               ARGS_NONE },
    { OPT_2,         '2', NULL,               ARGS_NONE },
    { OPT_3,         '3', NULL,               ARGS_NONE },
    { OPT_CHECK,     0,   "check-order",      ARGS_NONE },
    { OPT_NOCHECK,   0,   "nocheck-order",    ARGS_NONE },
    { OPT_DELIMITER, 0,   "output-delimiter", ARGS_REQUIRED },
    { OPT_TOTAL,     0,   "total",            ARGS_NONE },
    { OPT_ZERO,      'z', "zero-terminated",  ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

enum { CHECK_DEFAULT, CHECK_ENABLED, CHECK_DISABLED };

typedef struct {
    int         show[3];
    int         check;
    const char* delimiter;
    int         total;
    int         terminator;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
    case OPT_1: o->show[0] = 0; break;
    case OPT_2: o->show[1] = 0; break;
    case OPT_3: o->show[2] = 0; break;
    case OPT_CHECK: o->check = CHECK_ENABLED; break;
    case OPT_NOCHECK: o->check = CHECK_DISABLED; break;
    case OPT_DELIMITER:
        if (o->delimiter != NULL && strcmp(o->delimiter, value) != 0) {
            fprintf(stderr, "%s: multiple output delimiters specified\n", PROGRAM);
            return -1;
        }
        if (*value == '\0') {
            fprintf(stderr, "%s: empty %s not allowed\n", PROGRAM, "output delimiter");
            return -1;
        }
        o->delimiter = value;
        break;
    case OPT_TOTAL: o->total = 1; break;
    case OPT_ZERO: o->terminator = 0; break;
    default: break;
    }
    return 0;
}

/** One input: the reader, its current line, and a copy of the previous one to check the order. */
typedef struct {
    LineReader     reader;
    FILE*          file;
    TextLine       line;
    int            have;
    unsigned char* previous;
    size_t         previousLength, previousCapacity;
    int            hasPrevious;
    int            warned;
    int            number;
} Side;

static int compare_bytes(const unsigned char* a, size_t n, const unsigned char* b, size_t m) {
    size_t common = n < m ? n : m;
    int c = common > 0 ? memcmp(a, b, common) : 0;

    if (c != 0)
        return c;
    return n < m ? -1 : (n > m ? 1 : 0);
}

typedef struct {
    Options* options;
    Side     sides[2];
    int      seenUnpairable;
    long     counts[3];
} Comm;

/** Reads the next line of a side and checks it against the one before. Returns 1 to stop (order error). */
static int advance(Comm* c, Side* s) {
    if (s->have) {
        if (s->line.length > s->previousCapacity) {
            unsigned char* grown = realloc(s->previous, s->line.length + 64);
            if (grown == NULL) {
                fprintf(stderr, "%s: memory exhausted\n", PROGRAM);
                exit(1);
            }
            s->previous = grown;
            s->previousCapacity = s->line.length + 64;
        }
        if (s->line.length > 0)
            memcpy(s->previous, s->line.content, s->line.length);
        s->previousLength = s->line.length;
        s->hasPrevious = 1;
    }
    s->have = text_reader_next(&s->reader, &s->line);
    if (s->reader.failed) {
        fprintf(stderr, "%s: read error\n", PROGRAM);
        exit(1);
    }
    if (s->have && s->hasPrevious && c->options->check != CHECK_DISABLED &&
        (c->options->check == CHECK_ENABLED || c->seenUnpairable) && !s->warned &&
        compare_bytes(s->previous, s->previousLength, s->line.content, s->line.length) > 0) {
        fflush(stdout);
        fprintf(stderr, "%s: file %d is not in sorted order\n", PROGRAM, s->number);
        s->warned = 1;
        if (c->options->check == CHECK_ENABLED)
            return 1;
    }
    return 0;
}

/** Writes the line in a column: the delimiters of the columns before it that are shown, the line, its ending. */
static void write_column(const Comm* c, int column, const TextLine* line) {
    const Options* o = c->options;
    int i;

    if (!o->show[column])
        return;
    for (i = 0; i < column; i++)
        if (o->show[i])
            fputs(o->delimiter, stdout);
    fwrite(line->content, 1, line->length, stdout);
    if (o->terminator == 0) {
        putchar(0);
    } else if (line->ending == 2) {
        fputs("\r\n", stdout);
    } else {
        putchar('\n');
    }
}

static int open_side(Side* s, const char* name, int number, int terminator) {
    memset(s, 0, sizeof *s);
    s->number = number;
    if (text_open(PROGRAM, name, 1, &s->file) != 0)
        return -1;
    text_reader_init(&s->reader, s->file, terminator);
    return 0;
}

int main(int argc, char* argv[]) {
    Options o;
    Comm c;
    const char** operands;
    int count, result, status = 0;

    memset(&o, 0, sizeof o);
    o.show[0] = o.show[1] = o.show[2] = 1;
    o.terminator = '\n';
    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count != 2) {
        if (count == 0)
            fprintf(stderr, "%s: missing operand", PROGRAM);
        else if (count == 1)
            fprintf(stderr, "%s: missing operand after '%s'", PROGRAM, operands[0]);
        else
            fprintf(stderr, "%s: extra operand '%s'", PROGRAM, operands[2]);
        fputc('\n', stderr);
        args_usage_error(&spec);
        return 1;
    }
    if (o.delimiter == NULL)
        o.delimiter = "\t";

    text_binary_streams();
    memset(&c, 0, sizeof c);
    c.options = &o;
    if (open_side(&c.sides[0], operands[0], 1, o.terminator) != 0 ||
        open_side(&c.sides[1], operands[1], 2, o.terminator) != 0)
        return 1;

    if (advance(&c, &c.sides[0]) != 0 || advance(&c, &c.sides[1]) != 0)
        return 1;
    while (c.sides[0].have || c.sides[1].have) {
        int order;

        if (!c.sides[0].have)
            order = 1;
        else if (!c.sides[1].have)
            order = -1;
        else
            order = compare_bytes(c.sides[0].line.content, c.sides[0].line.length,
                                  c.sides[1].line.content, c.sides[1].line.length);
        if (order != 0)
            c.seenUnpairable = 1;
        if (order < 0) {
            write_column(&c, 0, &c.sides[0].line);
            c.counts[0]++;
            if (advance(&c, &c.sides[0]) != 0)
                return 1;
        } else if (order > 0) {
            write_column(&c, 1, &c.sides[1].line);
            c.counts[1]++;
            if (advance(&c, &c.sides[1]) != 0)
                return 1;
        } else {
            write_column(&c, 2, &c.sides[0].line);
            c.counts[2]++;
            if (advance(&c, &c.sides[0]) != 0 || advance(&c, &c.sides[1]) != 0)
                return 1;
        }
    }

    if (o.total) {
        printf("%ld%s%ld%s%ld%stotal%c", c.counts[0], o.delimiter, c.counts[1], o.delimiter, c.counts[2], o.delimiter,
               o.terminator == 0 ? 0 : '\n');
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "%s: write error\n", PROGRAM);
        return 1;
    }
    if (c.sides[0].warned || c.sides[1].warned) {
        fprintf(stderr, "%s: input is not in sorted order\n", PROGRAM);
        status = 1;
    }
    text_reader_free(&c.sides[0].reader);
    text_reader_free(&c.sides[1].reader);
    return status;
}
