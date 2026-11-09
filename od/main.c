/*
 * od: writes files in octal and other forms, as GNU od does: -A the address radix, -j -N what part is dumped, -t the
 * types (a c d o u x f with their sizes, z for the text at the end), the shorthand letters, -v (no "*" for equal lines),
 * -w the line width, -S the strings, --endian. Several types are written one under the other, aligned.
 * Not provided: the long double type (-t fL, f16): this compiler has no 80-bit type.
 */

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
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
#define PROGRAM "od"

static void print_usage(void) {
    puts("Usage: od [OPTION]... [FILE]...\n"
         "Write an unambiguous representation, octal bytes by default,\n"
         "of FILE to standard output.\n"
         "\n"
         "With more than one FILE argument, concatenate them in the listed order\n"
         "to form the input.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "If first and second call formats both apply, the second format is assumed\n"
         "if the last operand begins with + or (if there are 2 operands) a digit.\n"
         "An OFFSET operand means -j OFFSET.  LABEL is the pseudo-address\n"
         "at first byte printed, incremented when dump is progressing.\n"
         "For OFFSET and LABEL, a 0x or 0X prefix indicates hexadecimal;\n"
         "suffixes may be . for octal and b for multiply by 512.\n"
         "\n"
         "  -A, --address-radix=RADIX   output format for file offsets; RADIX is one\n"
         "                                of [doxn], for Decimal, Octal, Hex or None\n"
         "      --endian={big|little}   swap input bytes according the specified order\n"
         "  -j, --skip-bytes=BYTES      skip BYTES input bytes first\n"
         "  -N, --read-bytes=BYTES      limit dump to BYTES input bytes\n"
         "  -S, --strings[=BYTES]       output strings of at least BYTES graphic chars;\n"
         "                                3 is implied when BYTES is not specified\n"
         "  -t, --format=TYPE           select output format or formats\n"
         "  -v, --output-duplicates     do not use * to mark line suppression\n"
         "  -w[BYTES], --width[=BYTES]  output BYTES bytes per output line;\n"
         "                                32 is implied when BYTES is not specified\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Traditional format specifications may be intermixed; they accumulate:\n"
         "  -a   same as -t a,  select named characters, ignoring high-order bit\n"
         "  -b   same as -t o1, select octal bytes\n"
         "  -c   same as -t c,  select printable characters or backslash escapes\n"
         "  -d   same as -t u2, select unsigned decimal 2-byte units\n"
         "  -f   same as -t fF, select floats\n"
         "  -i   same as -t dI, select decimal ints\n"
         "  -l   same as -t dL, select decimal longs\n"
         "  -o   same as -t o2, select octal 2-byte units\n"
         "  -s   same as -t d2, select decimal 2-byte units\n"
         "  -x   same as -t x2, select hexadecimal 2-byte units\n"
         "\n"
         "TYPE is made up of one or more of these specifications:\n"
         "\n"
         "  a          named character, ignoring high-order bit\n"
         "  c          printable character or backslash escape\n"
         "  d[SIZE]    signed decimal, SIZE bytes per integer\n"
         "  f[SIZE]    floating point, SIZE bytes per float\n"
         "  o[SIZE]    octal, SIZE bytes per integer\n"
         "  u[SIZE]    unsigned decimal, SIZE bytes per integer\n"
         "  x[SIZE]    hexadecimal, SIZE bytes per integer\n"
         "\n"
         "SIZE is a number.  For TYPE in [doux], SIZE may also be C for\n"
         "sizeof(char), S for sizeof(short), I for sizeof(int) or L for\n"
         "sizeof(long).  If TYPE is f, SIZE may also be F for sizeof(float), D\n"
         "for sizeof(double) or L for sizeof(long double).\n"
         "\n"
         "Adding a z suffix to any type displays printable characters at the end of\n"
         "each output line.");
}

enum {
    OPT_ADDRESS = 1, OPT_ENDIAN, OPT_SKIP, OPT_READ, OPT_STRINGS, OPT_STRINGS_LONG, OPT_FORMAT, OPT_VERBOSE, OPT_WIDTH, OPT_TRADITIONAL,
    OPT_A, OPT_B, OPT_C, OPT_D, OPT_F, OPT_I, OPT_L, OPT_O, OPT_S, OPT_X, OPT_H
};

static const ArgsOption options[] = {
    { OPT_ADDRESS,     'A', "address-radix",     ARGS_REQUIRED },
    { OPT_ENDIAN,      0,   "endian",            ARGS_REQUIRED },
    { OPT_SKIP,        'j', "skip-bytes",        ARGS_REQUIRED },
    { OPT_READ,        'N', "read-bytes",        ARGS_REQUIRED },
    { OPT_STRINGS,     'S', NULL,                ARGS_REQUIRED },
    { OPT_STRINGS_LONG, 0,  "strings",           ARGS_OPTIONAL },
    { OPT_FORMAT,      't', "format",            ARGS_REQUIRED },
    { OPT_VERBOSE,     'v', "output-duplicates", ARGS_NONE },
    { OPT_WIDTH,       'w', "width",             ARGS_OPTIONAL },
    { OPT_TRADITIONAL, 0,   "traditional",       ARGS_NONE },
    { OPT_A, 'a', NULL, ARGS_NONE }, { OPT_B, 'b', NULL, ARGS_NONE }, { OPT_C, 'c', NULL, ARGS_NONE },
    { OPT_D, 'd', NULL, ARGS_NONE }, { OPT_F, 'f', NULL, ARGS_NONE }, { OPT_I, 'i', NULL, ARGS_NONE },
    { OPT_L, 'l', NULL, ARGS_NONE }, { OPT_O, 'o', NULL, ARGS_NONE }, { OPT_S, 's', NULL, ARGS_NONE },
    { OPT_X, 'x', NULL, ARGS_NONE }, { OPT_H, 'h', NULL, ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

#define MAX_SPECS 64

typedef struct {
    char   kind;                // a c d o u x f
    int    size;                // bytes of one value
    int    fieldWidth;          // of a value, without the blank in front
    int    trailer;             // z: the text of the line follows
    int    base, rest;          // extra blanks to line the types up: every value gets base, rest of them get one more
} Format;

typedef struct {
    int                radix;           // 'd' 'o' 'x' 'n'
    Format             formats[MAX_SPECS];
    int                count;
    unsigned long long skip;
    unsigned long long limit;           // bytes to dump; ULLONG_MAX none
    int                haveLimit;
    int                verbose;
    int                widthGiven;
    unsigned long long width;
    int                dumpStrings;     // -S given
    int                strings;         // the shortest string
    int                bigEndian;
} Config;

static void die(const char* message, const char* argument) {
    fprintf(stderr, PROGRAM ": ");
    fprintf(stderr, message, argument);
    fputc('\n', stderr);
    exit(1);
}

/** A byte count as xstrtoumax reads it: base 0 (0x.. hex, 0.. octal), then b k K m M G T P E, optionally with B or iB. */
static int parse_bytes(const char* text, unsigned long long* out) {
    char* end;
    unsigned long long value, factor = 1;
    const char* p = text;

    while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
        p++;
    if (*p == '-' || *p == '\0')
        return -1;
    value = strtoull(p, &end, 0);
    if (end == p)
        return -1;
    if (*end != '\0') {
        const char* s = end + 1;
        int decimal = 0;

        switch (*end) {
        case 'b': factor = 512; break;
        case 'k': case 'K': factor = 1024; break;
        case 'm': case 'M': factor = 1024ull * 1024; break;
        case 'G': factor = 1024ull * 1024 * 1024; break;
        case 'T': factor = 1024ull * 1024 * 1024 * 1024; break;
        case 'P': factor = 1024ull * 1024 * 1024 * 1024 * 1024; break;
        case 'E': factor = 1024ull * 1024 * 1024 * 1024 * 1024 * 1024; break;
        default: return -1;
        }
        if (*end != 'b') {
            if (*s == 'B') {
                decimal = 1;
                s++;
            } else if (s[0] == 'i' && s[1] == 'B') {
                s += 2;
            }
            if (decimal) {
                unsigned long long f = 1000, power = 1;
                int steps = *end == 'k' || *end == 'K' ? 1 : *end == 'm' || *end == 'M' ? 2 : *end == 'G' ? 3 :
                            *end == 'T' ? 4 : *end == 'P' ? 5 : 6;

                while (steps-- > 0)
                    power *= f;
                factor = power;
            }
        }
        if (*s != '\0')
            return -1;
    }
    if (factor != 1 && value > ULLONG_MAX / factor)
        value = ULLONG_MAX;
    else
        value *= factor;
    *out = value;
    return 0;
}

static const int octDigits[] = { 0, 3, 6, 8, 11, 14, 16, 19, 22 };
static const int signedDigits[] = { 0, 4, 6, 8, 11, 13, 16, 18, 20 };
static const int unsignedDigits[] = { 0, 3, 5, 8, 10, 13, 15, 17, 20 };
static const int hexDigits[] = { 0, 2, 4, 6, 8, 10, 12, 14, 16 };

static void add_format(Config* c, char kind, int size, int trailer) {
    Format* f;

    if (c->count == MAX_SPECS)
        die("too many output types%s", "");
    f = &c->formats[c->count++];
    memset(f, 0, sizeof *f);
    f->kind = kind;
    f->size = size;
    f->trailer = trailer;
    switch (kind) {
    case 'a':
    case 'c': f->fieldWidth = 3; break;
    case 'd': f->fieldWidth = signedDigits[size]; break;
    case 'u': f->fieldWidth = unsignedDigits[size]; break;
    case 'o': f->fieldWidth = octDigits[size]; break;
    case 'x': f->fieldWidth = hexDigits[size]; break;
    case 'f': f->fieldWidth = size == 4 ? 15 : 24; break;
    default: break;
    }
}

/** Reads the type string: a list of specifications, each with its size and an optional z. */
static void decode_formats(Config* c, const char* text) {
    const char* s = text;

    while (*s != '\0') {
        char kind = *s;
        int size = 0, trailer = 0;

        switch (kind) {
        case 'a':
        case 'c':
            s++;
            size = 1;
            break;
        case 'd': case 'o': case 'u': case 'x': {
            s++;
            if (isdigit((unsigned char)*s)) {
                char* end;
                unsigned long n = strtoul(s, &end, 10);

                if (n != 1 && n != 2 && n != 4 && n != 8) {
                    fprintf(stderr, PROGRAM ": invalid type string '%s';\nthis system doesn't provide a %lu-byte integral type\n",
                            text, n);
                    exit(1);
                }
                size = (int)n;
                s = end;
            } else {
                switch (*s) {
                case 'C': size = 1; s++; break;
                case 'S': size = 2; s++; break;
                case 'I': size = 4; s++; break;
                case 'L': size = 8; s++; break;
                default: size = 4; break;
                }
            }
            break;
        }
        case 'f': {
            s++;
            if (isdigit((unsigned char)*s)) {
                char* end;
                unsigned long n = strtoul(s, &end, 10);

                if (n != 4 && n != 8) {
                    fprintf(stderr, PROGRAM ": invalid type string '%s';\nthis system doesn't provide a %lu-byte floating point type\n",
                            text, n);
                    exit(1);
                }
                size = (int)n;
                s = end;
            } else {
                switch (*s) {
                case 'F': size = 4; s++; break;
                case 'D': size = 8; s++; break;
                case 'L':
                    fprintf(stderr, PROGRAM ": invalid type string '%s';\nthis system doesn't provide a 16-byte floating point type\n", text);
                    exit(1);
                default: size = 8; break;
                }
            }
            break;
        }
        default:
            fprintf(stderr, PROGRAM ": invalid character '%c' in type string '%s'\n", *s, text);
            exit(1);
        }
        if (*s == 'z') {
            trailer = 1;
            s++;
        }
        add_format(c, kind, size, trailer);
    }
}

static int on_option(int id, const char* value, void* context) {
    Config* c = context;

    switch (id) {
    case OPT_ADDRESS:
        if (value[0] == 'd' || value[0] == 'o' || value[0] == 'x' || value[0] == 'n') {
            c->radix = value[0];
        } else {
            fprintf(stderr, PROGRAM ": invalid output address radix '%c'; it must be one character from [doxn]\n", value[0]);
            return -1;
        }
        break;
    case OPT_ENDIAN:
        if (strcmp(value, "big") == 0) {
            c->bigEndian = 1;
        } else if (strcmp(value, "little") == 0) {
            c->bigEndian = 0;
        } else {
            fprintf(stderr, PROGRAM ": invalid argument '%s' for '--endian'\nValid arguments are:\n  - 'little'\n  - 'big'\n", value);
            args_usage_error(&spec);
            return -1;
        }
        break;
    case OPT_SKIP:
        if (parse_bytes(value, &c->skip) != 0) {
            fprintf(stderr, PROGRAM ": invalid -j argument '%s'\n", value);
            return -1;
        }
        break;
    case OPT_READ:
        if (parse_bytes(value, &c->limit) != 0) {
            fprintf(stderr, PROGRAM ": invalid -N argument '%s'\n", value);
            return -1;
        }
        c->haveLimit = 1;
        break;
    case OPT_STRINGS:
    case OPT_STRINGS_LONG:
        c->dumpStrings = 1;
        if (value == NULL) {
            c->strings = 3;
        } else {
            unsigned long long n;

            if (parse_bytes(value, &n) != 0 || n > INT_MAX) {
                fprintf(stderr, PROGRAM ": invalid -S argument '%s'\n", value);
                return -1;
            }
            c->strings = (int)n;
        }
        break;
    case OPT_FORMAT: decode_formats(c, value); break;
    case OPT_VERBOSE: c->verbose = 1; break;
    case OPT_WIDTH:
        c->widthGiven = 1;
        if (value == NULL) {
            c->width = 32;
        } else if (parse_bytes(value, &c->width) != 0) {
            fprintf(stderr, PROGRAM ": invalid --width argument '%s'\n", value);
            return -1;
        }
        break;
    case OPT_TRADITIONAL: break;
    case OPT_A: add_format(c, 'a', 1, 0); break;
    case OPT_B: add_format(c, 'o', 1, 0); break;
    case OPT_C: add_format(c, 'c', 1, 0); break;
    case OPT_D: add_format(c, 'u', 2, 0); break;
    case OPT_F: add_format(c, 'f', 4, 0); break;
    case OPT_I: add_format(c, 'd', 4, 0); break;
    case OPT_L: add_format(c, 'd', 8, 0); break;
    case OPT_O: add_format(c, 'o', 2, 0); break;
    case OPT_S: add_format(c, 'd', 2, 0); break;
    case OPT_X: add_format(c, 'x', 2, 0); break;
    case OPT_H: add_format(c, 'x', 2, 0); break;
    default: break;
    }
    return 0;
}

/** The input: the files one after the other, with the skipping and the limit. */
typedef struct {
    const char** names;
    int          count, next;
    FILE*        current;
    int          failed;                // a file could not be opened
    int          opened;                // a file could be opened
} Input;

static void next_file(Input* in) {
    while (in->current == NULL && in->next < in->count) {
        const char* name = in->names[in->next++];
        FILE* file;
        char native[VPATH_MAX];
        DWORD attributes;

        if (strcmp(name, "-") != 0 && vpath_to_native(name, native, sizeof native) == 0 &&
            fs_attributes(native, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            fprintf(stderr, PROGRAM ": %s: read error: Is a directory\n", name);      // opened, and then not readable
            in->failed = 1;
            in->opened = 1;
            continue;
        }
        if (text_open(PROGRAM, name, 1, &file) != 0) {
            in->failed = 1;
            continue;
        }
        in->current = file;
        in->opened = 1;
    }
}

/** Reads up to size bytes across the files; returns the number (0 at the end). */
static size_t input_read(Input* in, unsigned char* buffer, size_t size) {
    size_t got = 0;

    while (got < size) {
        long n;

        if (in->current == NULL) {
            next_file(in);
            if (in->current == NULL)
                break;
        }
        n = text_read_some(in->current, buffer + got, size - got);
        if (n <= 0) {
            if (in->current != stdin)
                fclose(in->current);
            in->current = NULL;
            continue;
        }
        got += (size_t)n;
    }
    return got;
}

static const char* const asciiNames[34] = {
    "nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel", "bs", "ht", "nl", "vt", "ff", "cr", "so", "si",
    "dle", "dc1", "dc2", "dc3", "dc4", "nak", "syn", "etb", "can", "em", "sub", "esc", "fs", "gs", "rs", "us", "sp", "del"
};

static void shortest_float(char* out, size_t size, double value, int isFloat) {
    double magnitude = value < 0 ? -value : value;
    int digits = magnitude < (isFloat ? FLT_MIN : DBL_MIN) ? 1 : (isFloat ? FLT_DIG : DBL_DIG);

    for (;; digits++) {
        snprintf(out, size, "%.*g", digits, value);
        if (isFloat ? (float)strtod(out, NULL) == (float)value : strtod(out, NULL) == value)
            return;
        if (digits >= (isFloat ? FLT_DIG + 4 : DBL_DIG + 4))
            return;
    }
}

/** The text of one value of the format. */
static void format_value(const Format* f, const unsigned char* bytes, int bigEndian, char* out, size_t size) {
    uint64_t v = 0;
    int i;

    for (i = 0; i < f->size; i++) {
        v = (v << 8) | bytes[bigEndian ? i : (f->size - 1 - i)];     // the first byte comes first in big endian
    }
    switch (f->kind) {
    case 'a': {
        unsigned c = (unsigned)(v & 0x7F);

        if (c <= 32)
            snprintf(out, size, "%s", asciiNames[c]);
        else if (c == 127)
            snprintf(out, size, "%s", asciiNames[33]);
        else
            snprintf(out, size, "%c", (int)c);
        break;
    }
    case 'c': {
        unsigned c = (unsigned)(v & 0xFF);

        switch (c) {
        case 0: snprintf(out, size, "\\0"); break;
        case 7: snprintf(out, size, "\\a"); break;
        case 8: snprintf(out, size, "\\b"); break;
        case 9: snprintf(out, size, "\\t"); break;
        case 10: snprintf(out, size, "\\n"); break;
        case 11: snprintf(out, size, "\\v"); break;
        case 12: snprintf(out, size, "\\f"); break;
        case 13: snprintf(out, size, "\\r"); break;
        default:
            if (c >= 32 && c < 127)
                snprintf(out, size, "%c", (int)c);
            else
                snprintf(out, size, "%03o", c);
        }
        break;
    }
    case 'd': {
        int64_t s = (int64_t)(v << (64 - 8 * f->size)) >> (64 - 8 * f->size);

        snprintf(out, size, "%lld", (long long)s);
        break;
    }
    case 'u': snprintf(out, size, "%llu", (unsigned long long)v); break;
    case 'o': snprintf(out, size, "%0*llo", f->fieldWidth, (unsigned long long)v); break;
    case 'x': snprintf(out, size, "%0*llx", f->fieldWidth, (unsigned long long)v); break;
    case 'f': {
        double d;
        int negative;

        if (f->size == 4) {
            uint32_t bits = (uint32_t)v;
            float x;

            memcpy(&x, &bits, 4);
            d = x;
            negative = (bits >> 31) != 0;
            if (x != x) {
                snprintf(out, size, "%s", negative ? "-nan" : "nan");
                break;
            }
            if (isinf(d)) {
                snprintf(out, size, "%s", negative ? "-inf" : "inf");
                break;
            }
            shortest_float(out, size, d, 1);
        } else {
            uint64_t bits = v;

            memcpy(&d, &bits, 8);
            negative = (bits >> 63) != 0;
            if (d != d) {
                snprintf(out, size, "%s", negative ? "-nan" : "nan");
                break;
            }
            if (isinf(d)) {
                snprintf(out, size, "%s", negative ? "-inf" : "inf");
                break;
            }
            shortest_float(out, size, d, 0);
        }
        break;
    }
    default: out[0] = '\0';
    }
}

static int addressWidth;
static int radixChar;

static void format_address(unsigned long long offset, int after) {
    switch (radixChar) {
    case 'd': printf("%07llu", offset); break;
    case 'o': printf("%07llo", offset); break;
    case 'x': printf("%06llx", offset); break;
    default: return;
    }
    if (after != 0)
        putchar(after);
}

static int lcm_of(int a, int b) {
    int x = a, y = b;

    while (y != 0) {
        int t = x % y;

        x = y;
        y = t;
    }
    return a / x * b;
}

static void write_block(const Config* c, unsigned long long offset, size_t n, const unsigned char* block, size_t blockSize) {
    int i;

    for (i = 0; i < c->count; i++) {
        const Format* f = &c->formats[i];
        size_t elements = (n + (size_t)f->size - 1) / (size_t)f->size, k, total = blockSize / (size_t)f->size;
        size_t missing = 0;

        if (i == 0)
            format_address(offset, 0);
        else
            printf("%*s", addressWidth, "");
        for (k = 0; k < elements; k++) {
            char text[64];
            int extra = f->base + (int)((((k + 1) * (size_t)f->rest + total - 1) / total) - ((k * (size_t)f->rest + total - 1) / total));

            format_value(f, block + k * (size_t)f->size, c->bigEndian, text, sizeof text);
            printf("%*s%*s", 1 + extra, "", f->fieldWidth, text);
        }
        if (f->trailer) {
            for (k = elements; k < total; k++)
                missing += (size_t)(1 + f->fieldWidth + f->base + (int)((((k + 1) * (size_t)f->rest + total - 1) / total) -
                                                                      ((k * (size_t)f->rest + total - 1) / total)));
            printf("%*s  >", (int)missing, "");
            for (k = 0; k < n; k++)
                putchar(block[k] >= 32 && block[k] < 127 ? block[k] : '.');
            putchar('<');
        }
        putchar('\n');
    }
}

static void dump_strings(const Config* c, Input* in, unsigned long long address) {
    unsigned long long end = c->haveLimit ? c->skip + c->limit : ULLONG_MAX;
    size_t capacity = c->strings > 100 ? (size_t)c->strings : 100, n;
    unsigned char* buf = malloc(capacity);
    int ch;

    if (buf == NULL)
        die("memory exhausted%s", "");
#define GETC(var) do { unsigned char b_; if (input_read(in, &b_, 1) == 0) { var = -1; } else { var = b_; } } while (0)
    for (;;) {
        size_t i;

    tryline:
        if (c->haveLimit && (end < (unsigned long long)c->strings || end - (unsigned long long)c->strings <= address))
            break;
        for (i = 0; i < (size_t)c->strings; i++) {
            GETC(ch);
            if (ch < 0)
                goto done;
            address++;
            if (ch == 0 || ch < 32 || ch > 126)
                goto tryline;
            buf[i] = (unsigned char)ch;
        }
        while (!c->haveLimit || address < end) {
            if (i == capacity) {
                capacity *= 2;
                buf = realloc(buf, capacity);
                if (buf == NULL)
                    die("memory exhausted%s", "");
            }
            GETC(ch);
            if (ch < 0)
                goto done;
            address++;
            if (ch == 0)
                break;
            if (ch < 32 || ch > 126)
                goto tryline;
            buf[i++] = (unsigned char)ch;
        }
        format_address(address - i - 1, ' ');
        for (n = 0; n < i; n++)
            putchar(buf[n]);
        putchar('\n');
    }
done:
    free(buf);
}

int main(int argc, char* argv[]) {
    Config c;
    Input in;
    const char** operands;
    int count, result, i, lcm = 1;
    size_t blockSize, got;
    unsigned char *prev, *cur, *tmp;
    unsigned long long offset;
    int first = 1, prevEqual = 0, widthBlock = 0;
    unsigned long long toSkip;
    int status = 0;

    memset(&c, 0, sizeof c);
    c.radix = 'o';
    c.limit = ULLONG_MAX;
    text_binary_streams();
    result = args_parse(&spec, argc, argv, on_option, &c, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (c.dumpStrings && c.count > 0) {
        fprintf(stderr, PROGRAM ": no type may be specified when dumping strings\n");
        return 1;
    }
    if (c.count == 0)
        add_format(&c, 'o', 2, 0);
    radixChar = c.radix;
    addressWidth = c.radix == 'n' ? 0 : (c.radix == 'x' ? 6 : 7);

    for (i = 0; i < c.count; i++)
        lcm = lcm_of(lcm, c.formats[i].size);
    if (c.widthGiven) {
        if (c.width == 0 || c.width % (unsigned long long)lcm != 0) {
            fprintf(stderr, PROGRAM ": warning: invalid width %llu; using %d instead\n", c.width, lcm);
            blockSize = (size_t)lcm;
        } else {
            blockSize = (size_t)c.width;
        }
    } else {
        blockSize = lcm < 16 ? (size_t)(lcm * (16 / lcm)) : (size_t)lcm;
    }
    for (i = 0; i < c.count; i++) {
        int fields = (int)(blockSize / (size_t)c.formats[i].size);
        int width = (c.formats[i].fieldWidth + 1) * fields;

        if (width > widthBlock)
            widthBlock = width;
    }
    for (i = 0; i < c.count; i++) {
        int fields = (int)(blockSize / (size_t)c.formats[i].size);
        int excess = widthBlock - (c.formats[i].fieldWidth + 1) * fields;

        c.formats[i].base = excess / fields;
        c.formats[i].rest = excess % fields;
    }

    memset(&in, 0, sizeof in);
    in.names = operands;
    in.count = count;
    if (count == 0) {
        static const char* standard[] = { "-" };

        in.names = standard;
        in.count = 1;
    }

    next_file(&in);                         // the first file is opened at once (an unreadable one is reported now)
    // skip: whole files that are shorter than the skip are passed
    toSkip = c.skip;
    while (toSkip > 0) {
        unsigned char skipBuffer[8192];
        size_t want = toSkip < sizeof skipBuffer ? (size_t)toSkip : sizeof skipBuffer;
        size_t n = input_read(&in, skipBuffer, want);

        if (n == 0)
            break;
        toSkip -= n;
    }
    if (toSkip > 0) {
        fprintf(stderr, PROGRAM ": cannot skip past end of combined input\n");
        return 1;
    }

    if (c.dumpStrings) {
        dump_strings(&c, &in, c.skip);
        if (fflush(stdout) != 0)
            return 1;
        return in.failed ? 1 : 0;
    }

    prev = calloc(1, blockSize);
    cur = calloc(1, blockSize);
    if (prev == NULL || cur == NULL)
        die("memory exhausted%s", "");
    offset = c.skip;
    {
        unsigned long long left = c.haveLimit ? c.limit : ULLONG_MAX;

        while (left > 0) {
            size_t want = left < blockSize ? (size_t)left : blockSize;

            got = input_read(&in, cur, want);
            if (got == 0)
                break;
            if (got < blockSize)
                memset(cur + got, 0, blockSize - got);
            if (!c.verbose && !first && got == blockSize && memcmp(prev, cur, blockSize) == 0) {
                if (!prevEqual) {
                    prevEqual = 1;
                    puts("*");
                }
            } else {
                prevEqual = 0;
                write_block(&c, offset, got, cur, blockSize);
            }
            first = 0;
            offset += got;
            left -= got;
            tmp = prev;
            prev = cur;
            cur = tmp;
            if (got < want)
                break;
        }
    }
    if (in.opened)
        format_address(offset, '\n');
    if (fflush(stdout) != 0) {
        fprintf(stderr, PROGRAM ": write error\n");
        return 1;
    }
    status = in.failed ? 1 : 0;
    return status;
}
