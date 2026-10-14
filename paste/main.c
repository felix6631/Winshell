#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"
#include "args.h"
#include "vec.h"

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

/** The inputs of paste: a reader for each operand ("-" operands share one) and how the lines end. */
typedef struct {
    LineReader** readers;
    const char** names;
    size_t       count;
    LineReader   stdinReader;
    int          haveStdin;
    int          terminator;    // '\n', or NUL with -z
    int          interactive;   // standard input is involved: write each line at once
} Inputs;

/** Opens the files. Returns 0, or 1 after a message (nothing is written then, as with GNU paste). */
static int open_inputs(Inputs* inputs, const char** names, size_t count, int terminator) {
    size_t i;

    inputs->names = names;
    inputs->count = count;
    inputs->terminator = terminator;
    inputs->haveStdin = 0;
    inputs->readers = calloc(count, sizeof *inputs->readers);
    if (inputs->readers == NULL) {
        fprintf(stderr, PROGRAM ": out of memory\n");
        return 1;
    }

    for (i = 0; i < count; i++) {
        if (strcmp(names[i], "-") == 0) {
            if (!inputs->haveStdin) {
                text_reader_init(&inputs->stdinReader, stdin, terminator);
                inputs->haveStdin = 1;
            }
            inputs->readers[i] = &inputs->stdinReader;
        } else {
            FILE* in;
            if (text_open(PROGRAM, names[i], 1, &in) != 0)
                return 1;
            inputs->readers[i] = malloc(sizeof **inputs->readers);
            if (inputs->readers[i] == NULL) {
                fprintf(stderr, PROGRAM ": out of memory\n");
                return 1;
            }
            text_reader_init(inputs->readers[i], in, terminator);
        }
    }
    inputs->interactive = inputs->haveStdin;
    return 0;
}

/** Reports the inputs that failed to be read. Returns the exit status: 0 or 1. */
static int read_errors(const Inputs* inputs) {
    size_t i;
    int status = 0;

    for (i = 0; i < inputs->count; i++) {
        if (inputs->readers[i] != NULL && inputs->readers[i]->failed) {
            fprintf(stderr, PROGRAM ": %s: read error\n", inputs->names[i]);
            status = 1;
        }
    }
    return status;
}

/** -s: the lines of each file are joined into one line. */
static void paste_serial(const Inputs* inputs, int zero) {
    size_t i;

    for (i = 0; i < inputs->count; i++) {
        TextLine line;
        size_t position = 0;
        int first = 1, crlf = 0;

        while (inputs->readers[i] != NULL && text_reader_next(inputs->readers[i], &line)) {
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
        if (crlf && !zero)
            fputc('\r', stdout);
        fputc(inputs->terminator, stdout);
        if (inputs->interactive)
            fflush(stdout);
    }
}

/**
 * The lines of the files side by side. The line is put together first: lines of one shared reader
 * do not stay valid, and a round in which every file is at its end writes nothing.
 * Returns 0, or 1 when out of memory.
 */
static int paste_parallel(const Inputs* inputs, int zero) {
    unsigned char* assembled = NULL;
    size_t assembledCapacity = 0;

    for (;;) {
        size_t position = 0, assembledLength = 0, i;
        int any = 0, crlf = 0;

        for (i = 0; i < inputs->count; i++) {
            TextLine line;
            int got = inputs->readers[i] != NULL && text_reader_next(inputs->readers[i], &line);
            const char* text = NULL;
            size_t length = 0, need;

            if (i > 0)
                next_delimiter(delimiters, &position, &text, &length);
            need = assembledLength + length + (got ? line.length : 0) + 2;
            if (VEC_RESERVE(assembled, assembledCapacity, need) != 0) {
                fprintf(stderr, PROGRAM ": out of memory\n");
                free(assembled);
                return 1;
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
        assembled[assembledLength++] = (unsigned char)inputs->terminator;
        fwrite(assembled, 1, assembledLength, stdout);
        if (inputs->interactive)
            fflush(stdout);
    }
    free(assembled);
    return 0;
}

int main(int argc, char* argv[]) {
    Options o = { 0, 0 };
    const char** operands;
    Inputs inputs;
    int count, result, status;

    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    if (delimiters[0] == '\0')
        delimiters = "\\0";            // an empty list is the same as \0: no delimiter at all
    if (check_delimiters(delimiters) != 0)
        return 1;

    text_binary_streams();
    count = (int)text_default_operands(operands, (size_t)count);

    if (open_inputs(&inputs, operands, (size_t)count, o.zero ? '\0' : '\n') != 0)
        return 1;

    if (o.serial) {
        paste_serial(&inputs, o.zero);
    } else if (paste_parallel(&inputs, o.zero) != 0) {
        return 1;
    }
    status = read_errors(&inputs);

    if (fflush(stdout) != 0) {
        fprintf(stderr, PROGRAM ": write error\n");
        return 1;
    }
    return status;
}
