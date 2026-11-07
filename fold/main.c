/*
 * fold: wraps the lines of its input to a width, as GNU fold does. A character counts as one column; a tab goes
 * to the next multiple of 8, a backspace goes back one and a carriage return back to the start. With -b the
 * bytes are counted instead, and with -s a line is broken after the last blank that fits.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "textutil.h"
#include "utf8.h"

#define VERSION "0.1"
#define PROGRAM "fold"

static void print_usage(void) {
    puts("Usage: fold [OPTION]... [FILE]...\n"
         "Wrap input lines in each FILE, writing to standard output.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -b, --bytes         count bytes rather than columns\n"
         "  -s, --spaces        break at spaces\n"
         "  -w, --width=WIDTH   use WIDTH columns instead of 80\n"
         "      --help          display this help and exit\n"
         "      --version       output version information and exit");
}

enum { OPT_BYTES = 1, OPT_SPACES, OPT_WIDTH };

static const ArgsOption options[] = {
    { OPT_BYTES,  'b', "bytes",  ARGS_NONE },
    { OPT_SPACES, 's', "spaces", ARGS_NONE },
    { OPT_WIDTH,  'w', "width",  ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 1 };

typedef struct {
    int       bytes;
    int       spaces;
    long long width;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    if (id == OPT_BYTES) {
        o->bytes = 1;
    } else if (id == OPT_SPACES) {
        o->spaces = 1;
    } else {
        char* end;

        // -5 and -w 5 both give the width
        o->width = strtoll(value, &end, 10);
        if (*value == '\0' || *end != '\0' || o->width <= 0) {
            fprintf(stderr, PROGRAM ": invalid number of columns: '%s'\n", value);
            return -1;
        }
    }
    return 0;
}

typedef struct {
    Options        o;
    unsigned char* line;        // what is waiting to be written: the part of the line since the last break
    size_t         length, capacity;
    long long      column;
} Folder;

static int add_bytes(Folder* f, const unsigned char* bytes, size_t n) {
    if (f->length + n > f->capacity) {
        size_t capacity = f->capacity == 0 ? 256 : f->capacity * 2;
        unsigned char* grown;

        while (capacity < f->length + n)
            capacity *= 2;
        grown = realloc(f->line, capacity);
        if (grown == NULL)
            return -1;
        f->line = grown;
        f->capacity = capacity;
    }
    memcpy(f->line + f->length, bytes, n);
    f->length += n;
    return 0;
}

/** The column after the character at c, given the column before it. */
static long long next_column(const Folder* f, unsigned char c, long long column) {
    if (f->o.bytes)
        return column + 1;
    if (c == '\r')
        return 0;
    if (c == '\b')
        return column > 0 ? column - 1 : 0;
    if (c == '\t')
        return column + 8 - column % 8;
    return column + 1;
}

/** The column at the end of the text (for what stays after a break at a blank). */
static long long column_of(const Folder* f, const unsigned char* text, size_t length) {
    long long column = 0;
    size_t i = 0;

    while (i < length) {
        size_t n = 1;

        if (!f->o.bytes && text[i] >= 0xC0)
            utf8_decode(text + i, length - i, &n);
        column = next_column(f, text[i], column);
        i += n;
    }
    return column;
}

/** Handles one character (its bytes are unit[0..n)). Returns 0, or -1 when out of memory. */
static int fold_unit(Folder* f, const unsigned char* unit, size_t n) {
    unsigned char c = unit[0];
    long long next;

    if (c == '\n') {
        if (add_bytes(f, unit, n) != 0)
            return -1;
        fwrite(f->line, 1, f->length, stdout);
        f->length = 0;
        f->column = 0;
        return 0;
    }
    for (;;) {
        next = next_column(f, c, f->column);
        if (next > f->o.width && f->length > 0) {
            if (f->o.spaces) {
                size_t end = f->length;
                int found = 0;

                while (end > 0) {
                    --end;
                    if (f->line[end] == ' ' || f->line[end] == '\t') {
                        found = 1;
                        break;
                    }
                }
                if (found) {
                    end++;
                    fwrite(f->line, 1, end, stdout);
                    putchar('\n');
                    memmove(f->line, f->line + end, f->length - end);
                    f->length -= end;
                    f->column = column_of(f, f->line, f->length);
                    continue;
                }
            }
            fwrite(f->line, 1, f->length, stdout);
            putchar('\n');
            f->length = 0;
            f->column = 0;
            continue;
        }
        break;
    }
    f->column = next;
    return add_bytes(f, unit, n);
}

static int fold_stream(Folder* f, FILE* in) {
    unsigned char buffer[65536];
    unsigned char pending[4];
    size_t pendingLength = 0;
    long got;

    // every file starts a line of its own; what is left at its end is written as it is
    f->length = 0;
    f->column = 0;
    while ((got = text_read_some(in, buffer, sizeof buffer)) > 0) {
        long i = 0;

        while (i < got) {
            unsigned char c = buffer[i];
            size_t n = 1;

            if (pendingLength == 0 && !f->o.bytes && c >= 0xC0) {
                // a character of several bytes: gather it, it may continue in the next chunk
                size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;

                if ((size_t)(got - i) >= need) {
                    size_t length;
                    utf8_decode(buffer + i, need, &length);
                    n = length;
                } else {
                    pendingLength = (size_t)(got - i);
                    memcpy(pending, buffer + i, pendingLength);
                    break;
                }
            }
            if (fold_unit(f, buffer + i, n) != 0)
                return -1;
            i += (long)n;
        }
        if (pendingLength > 0) {
            // complete the character from the next read
            long more = text_read_some(in, buffer, sizeof buffer);
            size_t need = pending[0] >= 0xF0 ? 4 : pending[0] >= 0xE0 ? 3 : 2, k = 0;

            while (more > 0 && pendingLength < need && k < (size_t)more)
                pending[pendingLength++] = buffer[k++];
            if (fold_unit(f, pending, pendingLength) != 0)
                return -1;
            pendingLength = 0;
            for (; (long)k < more; k++)
                if (fold_unit(f, buffer + k, 1) != 0)
                    return -1;
        }
        fflush(stdout);
    }
    if (f->length > 0)
        fwrite(f->line, 1, f->length, stdout);
    f->length = 0;
    return got < 0 ? 1 : 0;
}

int main(int argc, char* argv[]) {
    Folder folder;
    const char** operands;
    int count, result, i, status = 0;

    memset(&folder, 0, sizeof folder);
    text_binary_streams();
    folder.o.width = 80;
    result = args_parse(&spec, argc, argv, on_option, &folder.o, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    count = (int)text_default_operands(operands, (size_t)count);

    for (i = 0; i < count; i++) {
        FILE* in;
        int r;

        if (text_open(PROGRAM, operands[i], 1, &in) != 0) {
            status = 1;
            continue;
        }
        r = fold_stream(&folder, in);
        if (in != stdin)
            fclose(in);
        if (r != 0)
            status = 1;
    }
    free(folder.line);
    return fflush(stdout) != 0 ? 1 : status;
}
