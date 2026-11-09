/*
 * shuf: writes a random permutation of the input lines (or of the arguments with -e, or of a range of numbers with -i).
 * The permutation is made as GNU shuf makes it (a partial Fisher-Yates over the lines; a number in 0..N-1 is taken from
 * the random bytes by rejection and what is left over of them is kept), so --random-source gives the same result.
 * Without it the bytes come from the system.
 */

#define _CRT_RAND_S
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "textutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "shuf"

static void print_usage(void) {
    puts("Usage: shuf [OPTION]... [FILE]\n"
         "  or:  shuf -e [OPTION]... [ARG]...\n"
         "  or:  shuf -i LO-HI [OPTION]...\n"
         "Write a random permutation of the input lines to standard output.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  -e, --echo                treat each ARG as an input line\n"
         "  -i, --input-range=LO-HI   treat each number LO through HI as an input line\n"
         "  -n, --head-count=COUNT    output at most COUNT lines\n"
         "  -o, --output=FILE         write result to FILE instead of standard output\n"
         "      --random-source=FILE  get random bytes from FILE\n"
         "  -r, --repeat              output lines can be repeated\n"
         "  -z, --zero-terminated     line delimiter is NUL, not newline\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit");
}

enum { OPT_ECHO = 1, OPT_RANGE, OPT_COUNT, OPT_OUTPUT, OPT_SOURCE, OPT_REPEAT, OPT_ZERO };

static const ArgsOption options[] = {
    { OPT_ECHO,   'e', "echo",          ARGS_NONE },
    { OPT_RANGE,  'i', "input-range",   ARGS_REQUIRED },
    { OPT_COUNT,  'n', "head-count",    ARGS_REQUIRED },
    { OPT_OUTPUT, 'o', "output",        ARGS_REQUIRED },
    { OPT_SOURCE, 0,   "random-source", ARGS_REQUIRED },
    { OPT_REPEAT, 'r', "repeat",        ARGS_NONE },
    { OPT_ZERO,   'z', "zero-terminated", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int                echo, repeat, terminator;
    int                haveRange;
    unsigned long long lo, hi;
    unsigned long long head;            // at most this many lines
    const char*        output;
    const char*        source;
} Options;

/** Parses an unsigned decimal number as xstrtoumax does: blanks first, an optional +, no sign minus. 1 on overflow. */
static int parse_unsigned(const char* text, unsigned long long* out, int* overflow) {
    unsigned long long value = 0;
    const char* p = text;

    *overflow = 0;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
        p++;
    if (*p == '+')
        p++;
    if (*p < '0' || *p > '9')
        return -1;
    for (; *p >= '0' && *p <= '9'; p++) {
        unsigned digit = (unsigned)(*p - '0');

        if (value > (ULLONG_MAX - digit) / 10)
            *overflow = 1;
        else
            value = value * 10 + digit;
    }
    if (*p != '\0')
        return -1;
    *out = *overflow ? ULLONG_MAX : value;
    return 0;
}

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
    case OPT_ECHO: o->echo = 1; break;
    case OPT_REPEAT: o->repeat = 1; break;
    case OPT_ZERO: o->terminator = 0; break;
    case OPT_COUNT: {
        unsigned long long n;
        int overflow;

        if (parse_unsigned(value, &n, &overflow) != 0) {
            fprintf(stderr, PROGRAM ": invalid line count: '%s'\n", value);
            return -1;
        }
        if (n < o->head)
            o->head = n;
        break;
    }
    case OPT_OUTPUT:
        if (o->output != NULL && strcmp(o->output, value) != 0) {
            fprintf(stderr, PROGRAM ": multiple output files specified\n");
            return -1;
        }
        o->output = value;
        break;
    case OPT_SOURCE:
        if (o->source != NULL && strcmp(o->source, value) != 0) {
            fprintf(stderr, PROGRAM ": multiple random sources specified\n");
            return -1;
        }
        o->source = value;
        break;
    case OPT_RANGE: {
        const char* dash = strchr(value, '-');
        char lowText[64];
        int overflow;
        unsigned long long lo, hi, n;

        if (o->haveRange) {
            fprintf(stderr, PROGRAM ": multiple -i options specified\n");
            return -1;
        }
        o->haveRange = 1;
        if (dash == NULL) {
            fprintf(stderr, PROGRAM ": invalid input range: '%s'\n", value);
            return -1;
        }
        snprintf(lowText, sizeof lowText, "%.*s", (int)(dash - value), value);
        if ((size_t)(dash - value) >= sizeof lowText || parse_unsigned(lowText, &lo, &overflow) != 0 || overflow) {
            fprintf(stderr, PROGRAM ": invalid input range: '%.*s'\n", (int)(dash - value), value);
            return -1;
        }
        if (parse_unsigned(dash + 1, &hi, &overflow) != 0 || overflow) {
            fprintf(stderr, PROGRAM ": invalid input range: '%s'\n", dash + 1);
            return -1;
        }
        n = hi - lo + 1;
        if ((lo <= hi) == (n == 0)) {
            fprintf(stderr, PROGRAM ": invalid input range: '%s'\n", value);
            return -1;
        }
        o->lo = lo;
        o->hi = hi;
        break;
    }
    default: break;
    }
    return 0;
}

/** The random bytes: a file, or the system. What is left of the last bytes is kept for the next number. */
typedef struct {
    FILE*              file;
    const char*        name;
    unsigned long long number, maximum;
} Random;

static unsigned char random_byte(Random* r) {
    if (r->file != NULL) {
        int c = fgetc(r->file);

        if (c == EOF) {
            fflush(stdout);
            fprintf(stderr, PROGRAM ": '%s': end of file\n", r->name);
            exit(1);
        }
        return (unsigned char)c;
    } else {
        unsigned int v;

        if (rand_s(&v) != 0) {
            fprintf(stderr, PROGRAM ": cannot get random bytes\n");
            exit(1);
        }
        return (unsigned char)(v >> 8);
    }
}

/** A number in 0 .. count-1 (count is at least 1). */
static unsigned long long random_choose(Random* r, unsigned long long count) {
    unsigned long long genmax = count - 1;

    for (;;) {
        unsigned long long excess;

        if (r->maximum < genmax) {
            unsigned long long rmax = r->maximum;
            int bytes = 0, i;

            if (rmax > (ULLONG_MAX >> 8))
                r->number = r->maximum = rmax = 0;
            do {
                rmax = (rmax << 8) + 255;
                bytes++;
            } while (rmax < genmax);
            for (i = 0; i < bytes; i++)
                r->number = (r->number << 8) | random_byte(r);
            r->maximum = rmax;
        }
        // the numbers beyond the last whole block of COUNT of them are not used evenly: they are given back
        excess = (r->maximum % count + 1) % count;
        if (r->number <= r->maximum - excess) {
            unsigned long long result = r->number % count;

            r->number /= count;
            r->maximum = (r->maximum - excess) / count;
            return result;
        }
        r->number -= r->maximum - excess + 1;
        r->maximum = excess - 1;
    }
}

/** A table from an index to the number that was swapped into it: the permutation of a huge range without the range. */
typedef struct {
    unsigned long long* keys;
    unsigned long long* values;
    unsigned char*      used;
    size_t              size;
} Swaps;

static size_t swaps_slot(const Swaps* s, unsigned long long key) {
    size_t slot = (size_t)((key * 11400714819323198485ull) >> 20) & (s->size - 1);

    while (s->used[slot] && s->keys[slot] != key)
        slot = (slot + 1) & (s->size - 1);
    return slot;
}

static unsigned long long swaps_get(const Swaps* s, unsigned long long key) {
    size_t slot = swaps_slot(s, key);

    return s->used[slot] ? s->values[slot] : key;
}

static void swaps_set(Swaps* s, unsigned long long key, unsigned long long value) {
    size_t slot = swaps_slot(s, key);

    s->used[slot] = 1;
    s->keys[slot] = key;
    s->values[slot] = value;
}

typedef struct {
    const unsigned char* text;
    size_t               start, length;
} Line;

static int write_all(const void* data, size_t length, FILE* out) {
    return fwrite(data, 1, length, out) == length ? 0 : -1;
}

int main(int argc, char* argv[]) {
    Options o;
    Random random;
    const char** operands;
    int count, result;
    unsigned char* text = NULL;
    size_t length = 0, lineCount = 0, capacity = 0;
    Line* lines = NULL;
    unsigned long long n, h, i;
    FILE* out = stdout;
    unsigned char eol;

    memset(&o, 0, sizeof o);
    o.terminator = '\n';
    o.head = ULLONG_MAX;
    memset(&random, 0, sizeof random);
    text_binary_streams();
    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    eol = (unsigned char)o.terminator;

    if (o.echo && o.haveRange) {
        fprintf(stderr, PROGRAM ": cannot combine -e and -i options\n");
        args_usage_error(&spec);
        return 1;
    }
    if (!o.echo && (o.haveRange ? count > 0 : count > 1)) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[o.haveRange ? 0 : 1]);
        args_usage_error(&spec);
        return 1;
    }

    if (o.source != NULL) {
        char native[VPATH_MAX];

        random.name = o.source;
        if (vpath_to_native(o.source, native, sizeof native) != 0 || (random.file = fopen(native, "rb")) == NULL) {
            fprintf(stderr, PROGRAM ": %s: %s\n", o.source, fs_strerror(GetLastError()));
            return 1;
        }
    }

    // the lines: the arguments, the numbers of the range (not made, only counted) or the text of a file
    if (o.echo) {
        n = (unsigned long long)count;
    } else if (o.haveRange) {
        n = o.hi - o.lo + 1;
    } else {
        FILE* in;
        size_t pos = 0;
        const char* name = count > 0 ? operands[0] : "-";

        if (text_open(PROGRAM, name, 1, &in) != 0)
            return 1;
        text = text_slurp(in, &length);
        if (in != stdin)
            fclose(in);
        if (text == NULL) {
            fprintf(stderr, PROGRAM ": %s: read error\n", name);
            return 1;
        }
        while (pos < length) {
            unsigned char* end = memchr(text + pos, eol, length - pos);
            size_t stop = end != NULL ? (size_t)(end - text) : length;

            if (lineCount == capacity) {
                capacity = capacity == 0 ? 1024 : capacity * 2;
                lines = realloc(lines, capacity * sizeof *lines);
                if (lines == NULL) {
                    fprintf(stderr, PROGRAM ": memory exhausted\n");
                    return 1;
                }
            }
            lines[lineCount].text = text;
            lines[lineCount].start = pos;
            lines[lineCount].length = stop - pos;
            lineCount++;
            pos = stop + 1;
        }
        n = lineCount;
    }

    if (o.output != NULL) {
        char native[VPATH_MAX];

        if (vpath_to_native(o.output, native, sizeof native) != 0 || (out = fopen(native, "wb")) == NULL) {
            fprintf(stderr, PROGRAM ": %s: %s\n", o.output, fs_strerror(GetLastError()));
            return 1;
        }
    }

#define EMIT(k) do {                                                                                              \
        unsigned long long idx_ = (k);                                                                            \
        int bad_;                                                                                                 \
        if (o.echo) {                                                                                             \
            bad_ = write_all(operands[idx_], strlen(operands[idx_]), out) != 0;                                   \
        } else if (o.haveRange) {                                                                                 \
            char digits_[32];                                                                                     \
            int len_ = snprintf(digits_, sizeof digits_, "%llu", o.lo + idx_);                                    \
            bad_ = write_all(digits_, (size_t)len_, out) != 0;                                                    \
        } else {                                                                                                  \
            bad_ = write_all(lines[idx_].text + lines[idx_].start, lines[idx_].length, out) != 0;                 \
        }                                                                                                         \
        if (bad_ || fputc(eol, out) == EOF) {                                                                     \
            fprintf(stderr, PROGRAM ": write error\n");                                                           \
            return 1;                                                                                             \
        }                                                                                                         \
    } while (0)

    if (o.repeat) {
        if (n == 0 && o.head > 0) {
            fprintf(stderr, PROGRAM ": no lines to repeat\n");
            return 1;
        }
        for (i = 0; i < o.head; i++)
            EMIT(random_choose(&random, n));
    } else {
        Swaps swaps;
        unsigned long long* order;
        size_t size = 16;

        h = o.head < n ? o.head : n;
        if (h > (1ull << 28)) {
            fprintf(stderr, PROGRAM ": memory exhausted\n");
            return 1;
        }
        while ((unsigned long long)size < h * 2 + 2)
            size *= 2;
        swaps.size = size;
        swaps.keys = calloc(size, sizeof *swaps.keys);
        swaps.values = calloc(size, sizeof *swaps.values);
        swaps.used = calloc(size, 1);
        if (swaps.keys == NULL || swaps.values == NULL || swaps.used == NULL) {
            fprintf(stderr, PROGRAM ": memory exhausted\n");
            return 1;
        }
        order = malloc((h + 1) * sizeof *order);
        if (order == NULL) {
            fprintf(stderr, PROGRAM ": memory exhausted\n");
            return 1;
        }
        for (i = 0; i < h; i++) {            // all of it before the first line is written: the bytes may run out
            unsigned long long j = i + random_choose(&random, n - i);
            unsigned long long vi = swaps_get(&swaps, i), vj = swaps_get(&swaps, j);

            swaps_set(&swaps, j, vi);
            order[i] = vj;
        }
        for (i = 0; i < h; i++)
            EMIT(order[i]);
    }
    if (fflush(out) != 0) {
        fprintf(stderr, PROGRAM ": write error\n");
        return 1;
    }
    if (out != stdout)
        fclose(out);
    return 0;
}
