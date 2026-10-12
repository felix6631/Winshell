#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "paste"

static void print_usage(void) {
    puts("Usage: paste [OPTION]... [FILE]...\n"
         "Write lines consisting of the sequentially corresponding lines from each\n"
         "FILE, separated by TABs, to standard output.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -d, --delimiters=LIST   reuse characters from LIST instead of TABs\n"
         "  -s, --serial            paste one file at a time instead of in parallel\n"
         "  -z, --zero-terminated   line delimiter is NUL, not newline\n"
         "      --help              display this help and exit\n"
         "      --version           output version information and exit\n"
         "\n"
         "LIST characters are used in turn, starting again for each output line. The\n"
         "escapes \\n, \\t, \\\\ and \\0 (nothing) are understood. A file named - is\n"
         "standard input, and several of them take the lines of it in turn (paste - -).\n"
         "A CR in front of the line feed is part of the line ending: the lines are joined\n"
         "without it, and the output line ends with CR LF when an input line did.");
}

static const char* delimiters = "\t";       // the list as it is written

/** The delimiter number k of the list: returns its text and length ("\0" is an empty one). */
static int next_delimiter(const char* list, size_t* position, const char** text, size_t* length) {
    static char single[2];
    char c = list[*position];

    if (c == '\0') {            // the list starts again
        *position = 0;
        c = list[0];
    }
    if (c == '\\' && list[*position + 1] != '\0') {
        char e = list[*position + 1];
        *position += 2;
        switch (e) {
            case 'n': single[0] = '\n'; break;
            case 't': single[0] = '\t'; break;
            case '\\': single[0] = '\\'; break;
            case '0': *text = ""; *length = 0; return 0;
            default: single[0] = e; break;
        }
        *text = single;
        *length = 1;
        return 0;
    }
    single[0] = c;
    *text = single;
    *length = 1;
    (*position)++;
    return 0;
}

/** Checks the list: a backslash may not end it. */
static int check_delimiters(const char* list) {
    size_t length = strlen(list);
    {
        size_t i, backslashes = 0;
        for (i = length; i > 0 && list[i - 1] == '\\'; i--)
            backslashes++;
        if (backslashes % 2 == 1) {
            fprintf(stderr, PROGRAM ": delimiter list ends with an unescaped backslash: %s\n", list);
            return -1;
        }
    }
    return 0;
}

enum { OPT_DELIMITERS = 1, OPT_SERIAL, OPT_ZERO };

static const ArgsOption options[] = {
    { OPT_DELIMITERS, 'd', "delimiters",      ARGS_REQUIRED },
    { OPT_SERIAL,     's', "serial",          ARGS_NONE },
    { OPT_ZERO,       'z', "zero-terminated", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int serial, zero;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
        case OPT_DELIMITERS: delimiters = value; break;
        case OPT_SERIAL:     o->serial = 1; break;
        case OPT_ZERO:       o->zero = 1; break;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    Options o = { 0, 0 };
    const char** operands;
    size_t operandCount, i;
    int serial, zero, status = 0, count, result;
    LineReader** readers;
    LineReader stdinReader;
    int haveStdin = 0, terminator;

    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    operandCount = (size_t)count;
    serial = o.serial;
    zero = o.zero;

    if (delimiters[0] == '\0')
        delimiters = "\\0";            // an empty list is the same as \0: no delimiter at all
    if (check_delimiters(delimiters) != 0)
        return 1;

    text_binary_streams();
    if (operandCount == 0)
        operands[operandCount++] = "-";
    terminator = zero ? '\0' : '\n';

    readers = calloc(operandCount, sizeof *readers);
    if (readers == NULL) {
        fprintf(stderr, PROGRAM ": out of memory\n");
        return 1;
    }

    // every file has its reader; the several "-" share the one of standard input
    for (i = 0; i < operandCount; i++) {
        if (strcmp(operands[i], "-") == 0) {
            if (!haveStdin) {
                text_reader_init(&stdinReader, stdin, terminator);
                haveStdin = 1;
            }
            readers[i] = &stdinReader;
        } else {
            FILE* in;
            if (text_open(PROGRAM, operands[i], 1, &in) != 0)
                return 1;               // as GNU paste: nothing is written when a file is missing
            readers[i] = malloc(sizeof **readers);
            if (readers[i] == NULL) {
                fprintf(stderr, PROGRAM ": out of memory\n");
                return 1;
            }
            text_reader_init(readers[i], in, terminator);
        }
    }

    if (serial) {
        for (i = 0; i < operandCount; i++) {
            TextLine line;
            size_t position = 0;
            int first = 1, crlf = 0;

            if (readers[i] != NULL) {
                while (text_reader_next(readers[i], &line)) {
                    if (!first) {
                        const char* text;
                        size_t length;
                        next_delimiter(delimiters, &position, &text, &length);
                        fwrite(text, 1, length, stdout);
                    }
                    fwrite(line.content, 1, line.length, stdout);
                    if (line.ending == 2)
                        crlf = 1;
                    first = 0;
                }
                if (readers[i]->failed) {
                    fprintf(stderr, PROGRAM ": %s: read error\n", operands[i]);
                    status = 1;
                }
            }
            if (crlf && !zero)
                fputc('\r', stdout);
            fputc(terminator, stdout);
            if (haveStdin)
                fflush(stdout);
        }
    } else {
        // the line is put together first: lines of one shared reader do not stay valid, and
        // a round in which every file is at its end writes nothing
        unsigned char* assembled = NULL;
        size_t assembledLength, assembledCapacity = 0;

        for (;;) {
            size_t position = 0;
            int any = 0, crlf = 0;

            assembledLength = 0;
            for (i = 0; i < operandCount; i++) {
                TextLine line;
                int got = readers[i] != NULL && text_reader_next(readers[i], &line);
                const char* text = NULL;
                size_t length = 0, need;

                if (i > 0)
                    next_delimiter(delimiters, &position, &text, &length);
                need = assembledLength + length + (got ? line.length : 0) + 2;
                if (need > assembledCapacity) {
                    size_t capacity = assembledCapacity == 0 ? 4096 : assembledCapacity;
                    unsigned char* grown;
                    while (capacity < need)
                        capacity *= 2;
                    grown = realloc(assembled, capacity);
                    if (grown == NULL) {
                        fprintf(stderr, PROGRAM ": out of memory\n");
                        return 1;
                    }
                    assembled = grown;
                    assembledCapacity = capacity;
                }
                if (length > 0) {
                    memcpy(assembled + assembledLength, text, length);
                    assembledLength += length;
                }
                if (got) {
                    any = 1;
                    memcpy(assembled + assembledLength, line.content, line.length);
                    assembledLength += line.length;
                    if (line.ending == 2)
                        crlf = 1;
                }
            }
            if (!any)
                break;
            if (crlf && !zero)
                assembled[assembledLength++] = '\r';
            assembled[assembledLength++] = (unsigned char)terminator;
            fwrite(assembled, 1, assembledLength, stdout);
            if (haveStdin)
                fflush(stdout);
        }
        free(assembled);
        for (i = 0; i < operandCount; i++)
            if (readers[i] != NULL && readers[i]->failed) {
                fprintf(stderr, PROGRAM ": %s: read error\n", operands[i]);
                status = 1;
            }
    }

    if (fflush(stdout) != 0) {
        fprintf(stderr, PROGRAM ": write error\n");
        return 1;
    }
    return status;
}
