#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "basecode.h"
#include "textutil.h"

typedef struct {
    int       decode;
    int       ignoreGarbage;
    long long wrap;
} Settings;

enum { OPT_DECODE = 1, OPT_IGNORE, OPT_WRAP };

static const ArgsOption options[] = {
    { OPT_DECODE, 'd', "decode",         ARGS_NONE },
    { OPT_IGNORE, 'i', "ignore-garbage", ARGS_NONE },
    { OPT_WRAP,   'w', "wrap",           ARGS_REQUIRED },
    { 0 }
};

static const char* currentProgram;
static int currentBits;

static void print_usage(void) {
    printf("Usage: %s [OPTION]... [FILE]\n"
           "%s encode or decode FILE, or standard input, to standard output.\n"
           "\n"
           "With no FILE, or when FILE is -, read standard input.\n"
           "\n"
           "  -d, --decode          decode data\n"
           "  -i, --ignore-garbage  when decoding, ignore non-alphabet characters\n"
           "  -w, --wrap=COLS       wrap encoded lines after COLS character (default 76).\n"
           "                          Use 0 to disable line wrapping\n"
           "      --help            display this help and exit\n"
           "      --version         output version information and exit\n",
           currentProgram, currentBits == 64 ? "Base64" : "Base32");
}

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;

    if (id == OPT_DECODE) {
        s->decode = 1;
    } else if (id == OPT_IGNORE) {
        s->ignoreGarbage = 1;
    } else {
        char* end;

        s->wrap = strtoll(value, &end, 10);
        if (*value == '\0' || *end != '\0' || s->wrap < 0) {
            fprintf(stderr, "%s: invalid wrap size: '%s'\n", currentProgram, value);
            return -1;
        }
    }
    return 0;
}

static const char alphabet64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char alphabet32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

/** Writes the text in lines of `wrap` characters (all in one line when 0); each line ends with LF. */
typedef struct {
    long long wrap;
    long long column;
} Wrapper;

static void put_wrapped(Wrapper* w, char c) {
    if (w->wrap > 0 && w->column == w->wrap) {
        putchar('\n');
        w->column = 0;
    }
    putchar(c);
    w->column++;
}

static void encode(const unsigned char* data, size_t length, int bits, long long wrap) {
    Wrapper w = { wrap, 0 };
    const char* alphabet = bits == 64 ? alphabet64 : alphabet32;
    size_t i;

    if (bits == 64) {
        for (i = 0; i < length; i += 3) {
            size_t left = length - i;
            unsigned long v = (unsigned long)data[i] << 16;

            if (left > 1)
                v |= (unsigned long)data[i + 1] << 8;
            if (left > 2)
                v |= data[i + 2];
            put_wrapped(&w, alphabet[(v >> 18) & 63]);
            put_wrapped(&w, alphabet[(v >> 12) & 63]);
            put_wrapped(&w, left > 1 ? alphabet[(v >> 6) & 63] : '=');
            put_wrapped(&w, left > 2 ? alphabet[v & 63] : '=');
        }
    } else {
        for (i = 0; i < length; i += 5) {
            size_t left = length - i, k;
            unsigned long long v = 0;
            // the characters a group of this many bytes needs: 1 -> 2, 2 -> 4, 3 -> 5, 4 -> 7, 5 -> 8
            static const int chars[6] = { 0, 2, 4, 5, 7, 8 };
            int used = left > 5 ? 5 : (int)left;

            for (k = 0; k < 5; k++)
                v = (v << 8) | (k < (size_t)used ? data[i + k] : 0);
            for (k = 0; k < 8; k++) {
                if ((int)k < chars[used])
                    put_wrapped(&w, alphabet[(v >> (35 - 5 * k)) & 31]);
                else
                    put_wrapped(&w, '=');
            }
        }
    }
    if (w.column > 0 && w.wrap > 0)
        putchar('\n');
}

static int symbol_value(const char* alphabet, unsigned char c) {
    const char* at = c != '\0' ? strchr(alphabet, c) : NULL;

    return at != NULL ? (int)(at - alphabet) : -1;
}

/** Decodes; returns 0, or 1 for invalid input (what could be decoded before it has been written). */
static int decode(const unsigned char* data, size_t length, int bits, int ignoreGarbage) {
    const char* alphabet = bits == 64 ? alphabet64 : alphabet32;
    int group = bits == 64 ? 4 : 8, bitsPerChar = bits == 64 ? 6 : 5;
    int values[8];
    int n = 0, pads = 0;
    size_t i;

#define BYTES_OF(chars) ((chars) * bitsPerChar / 8)
    for (i = 0; i < length; i++) {
        unsigned char c = data[i];
        int v;
        int complete;

        if (c == '\n')
            continue;
        if (n + pads == group) {
            n = 0;
            pads = 0;
        }
        if (c == '=') {
            v = -2;
        } else {
            v = symbol_value(alphabet, c);
            if (v < 0 && ignoreGarbage)
                continue;
        }
        if (v == -1 || (v == -2 && n == 0) || (v >= 0 && pads > 0)) {
            // bad character, padding with nothing in front of it, or data after padding
            if (bits == 64) {
                unsigned long acc = 0;
                int k;

                for (k = 0; k < n; k++)
                    acc = (acc << 6) | (unsigned long)values[k];
                acc <<= 6 * (4 - n);
                for (k = 0; k < BYTES_OF(n); k++)
                    putchar((int)((acc >> (16 - 8 * k)) & 255));
            }
            return 1;
        }
        if (v == -2)
            pads++;
        else
            values[n++] = v;

        complete = n + pads == group;
        if (complete) {
            unsigned long long acc = 0;
            int k, valid;

            if (bits == 64)
                valid = n == 4 || n == 3 || n == 2;
            else
                valid = n == 8 || n == 7 || n == 5 || n == 4 || n == 2;
            for (k = 0; k < n; k++)
                acc = (acc << bitsPerChar) | (unsigned long long)values[k];
            acc <<= bitsPerChar * (group - n);
            for (k = 0; k < BYTES_OF(n); k++)
                putchar((int)((acc >> (bitsPerChar * group - 8 - 8 * k)) & 255));
            if (!valid || (n < group && pads != group - n))
                return 1;
        }
    }
    if (n + pads != 0 && n + pads != group) {
        // the end of the input in the middle of a group
        if (bits == 64) {
            unsigned long acc = 0;
            int k;

            for (k = 0; k < n; k++)
                acc = (acc << 6) | (unsigned long)values[k];
            acc <<= 6 * (4 - n);
            for (k = 0; k < BYTES_OF(n); k++)
                putchar((int)((acc >> (16 - 8 * k)) & 255));
        }
        return 1;
    }
#undef BYTES_OF
    return 0;
}

int basecode_main(const char* program, const char* version, int bits, int argc, char* argv[]) {
    ArgsSpec spec;
    Settings settings;
    const char** operands;
    int count, result;
    FILE* in;
    unsigned char* data;
    size_t length;
    int status = 0;

    currentProgram = program;
    currentBits = bits;
    memset(&spec, 0, sizeof spec);
    spec.program = program;
    spec.version = version;
    spec.usage = print_usage;
    spec.options = options;
    spec.usageStatus = 1;

    memset(&settings, 0, sizeof settings);
    settings.wrap = 76;
    text_binary_streams();
    result = args_parse(&spec, argc, argv, on_option, &settings, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 1) {
        fprintf(stderr, "%s: extra operand '%s'\n", program, operands[1]);
        args_usage_error(&spec);
        return 1;
    }
    if (text_open(program, count == 1 ? operands[0] : "-", 1, &in) != 0)
        return 1;
    data = text_slurp(in, &length);
    if (in != stdin)
        fclose(in);
    if (data == NULL) {
        fprintf(stderr, "%s: read error\n", program);
        return 1;
    }

    if (settings.decode) {
        if (decode(data, length, bits, settings.ignoreGarbage) != 0) {
            fflush(stdout);
            fprintf(stderr, "%s: invalid input\n", program);
            status = 1;
        }
    } else {
        encode(data, length, bits, settings.wrap);
    }
    free(data);
    return fflush(stdout) != 0 ? 1 : status;
}
