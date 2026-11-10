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
static const char alphabet32hex[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
static const char alphabet64url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

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

static void encode(const unsigned char* data, size_t length, int bits, const char* alphabet, long long wrap) {
    Wrapper w = { wrap, 0 };
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

    // GNU's base32hex passes W to Z on to the base32 decoder, which reads them as the letters 22 to 25
    if (at == NULL && alphabet == alphabet32hex && c >= 'W' && c <= 'Z')
        return c - 'W' + 22;
    return at != NULL ? (int)(at - alphabet) : -1;
}

/** Non-zero when a group of this many characters starts at index `start` (newlines do not count): all of it is there. */
static int group_is_there(const unsigned char* data, size_t length, size_t start, int group) {
    int seen = 0;

    for (; start < length && seen < group; start++)
        if (data[start] != '\n')
            seen++;
    return seen >= group;
}

/** Decodes; returns 0, or 1 for invalid input (what could be decoded before it has been written). */
static int decode(const unsigned char* data, size_t length, int bits, const char* alphabet, int ignoreGarbage) {
    int group = bits == 64 ? 4 : 8, bitsPerChar = bits == 64 ? 6 : 5;
    int values[8];
    int n = 0, pads = 0;
    size_t i, groupStart = 0;

    // base64url takes no + or /: GNU turns them down before it decodes anything
    if (alphabet == alphabet64url && !ignoreGarbage && (memchr(data, '+', length) != NULL || memchr(data, '/', length) != NULL))
        return 1;
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
        if (n + pads == 0)
            groupStart = i;
        if (c == '=') {
            v = -2;
        } else {
            v = symbol_value(alphabet, c);
            if (ignoreGarbage && alphabet == alphabet32hex && c >= 'W' && c <= 'Z')
                v = -1;                                 // -i filters by the real alphabet, before the quirk
            if (v < 0 && ignoreGarbage)
                continue;
        }
        if (v == -1 || (v == -2 && n == 0) || (v >= 0 && pads > 0)) {
            // bad character, padding with nothing in front of it, or data after padding
            // base64 writes what a group has before the bad character, base32 nothing; base32hex (which GNU translates to
            // base32 first) only when the whole group is in the input
            if (bits == 64 || (alphabet == alphabet32hex && group_is_there(data, length, groupStart, group))) {
                unsigned long long acc = 0;
                int k;

                for (k = 0; k < n; k++)
                    acc = (acc << bitsPerChar) | (unsigned long long)values[k];
                acc <<= bitsPerChar * (group - n);
                for (k = 0; k < BYTES_OF(n); k++)
                    putchar((int)((acc >> (bitsPerChar * group - 8 - 8 * k)) & 255));
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
        if (decode(data, length, bits, bits == 64 ? alphabet64 : alphabet32, settings.ignoreGarbage) != 0) {
            fflush(stdout);
            fprintf(stderr, "%s: invalid input\n", program);
            status = 1;
        }
    } else {
        encode(data, length, bits, bits == 64 ? alphabet64 : alphabet32, settings.wrap);
    }
    free(data);
    return fflush(stdout) != 0 ? 1 : status;
}

/* ---- basenc: the other encodings ------------------------------------------------------------- */

static const char alphabet16[] = "0123456789ABCDEF";
static const char alphabetZ85[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#";

typedef enum {
    ENC_NONE, ENC_BASE64, ENC_BASE64URL, ENC_BASE32, ENC_BASE32HEX, ENC_BASE16, ENC_BASE2MSBF, ENC_BASE2LSBF, ENC_Z85
} Encoding;

enum {
    BE_BASE64 = 1, BE_BASE64URL, BE_BASE32, BE_BASE32HEX, BE_BASE16, BE_BASE2MSBF, BE_BASE2LSBF, BE_Z85,
    BE_DECODE, BE_IGNORE, BE_WRAP
};

static const ArgsOption basencOptions[] = {
    { BE_BASE64,    0,   "base64",         ARGS_NONE },
    { BE_BASE64URL, 0,   "base64url",      ARGS_NONE },
    { BE_BASE32,    0,   "base32",         ARGS_NONE },
    { BE_BASE32HEX, 0,   "base32hex",      ARGS_NONE },
    { BE_BASE16,    0,   "base16",         ARGS_NONE },
    { BE_BASE2MSBF, 0,   "base2msbf",      ARGS_NONE },
    { BE_BASE2LSBF, 0,   "base2lsbf",      ARGS_NONE },
    { BE_Z85,       0,   "z85",            ARGS_NONE },
    { BE_DECODE,    'd', "decode",         ARGS_NONE },
    { BE_IGNORE,    'i', "ignore-garbage", ARGS_NONE },
    { BE_WRAP,      'w', "wrap",           ARGS_REQUIRED },
    { 0 }
};

typedef struct {
    Settings settings;
    Encoding encoding;
} BasencSettings;

static void print_basenc_usage(void) {
    puts("Usage: basenc [OPTION]... [FILE]\n"
         "basenc encode or decode FILE, or standard input, to standard output.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "      --base64          same as 'base64' program (RFC4648 section 4)\n"
         "      --base64url       file- and url-safe base64 (RFC4648 section 5)\n"
         "      --base32          same as 'base32' program (RFC4648 section 6)\n"
         "      --base32hex       extended hex alphabet base32 (RFC4648 section 7)\n"
         "      --base16          hex encoding (RFC4648 section 8)\n"
         "      --base2msbf       bit string with most significant bit (msb) first\n"
         "      --base2lsbf       bit string with least significant bit (lsb) first\n"
         "  -d, --decode          decode data\n"
         "  -i, --ignore-garbage  when decoding, ignore non-alphabet characters\n"
         "  -w, --wrap=COLS       wrap encoded lines after COLS character (default 76).\n"
         "                          Use 0 to disable line wrapping\n"
         "\n"
         "      --z85             ascii85-like encoding (ZeroMQ spec:32/Z85);\n"
         "                        when encoding, input length must be a multiple of 4;\n"
         "                        when decoding, input length must be a multiple of 5\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "When decoding, the input may contain newlines in addition to the bytes of\n"
         "the formal alphabet.  Use --ignore-garbage to attempt to recover\n"
         "from any other non-alphabet bytes in the encoded stream.");
}

static int on_basenc_option(int id, const char* value, void* context) {
    BasencSettings* b = context;

    switch (id) {
    case BE_BASE64: b->encoding = ENC_BASE64; break;
    case BE_BASE64URL: b->encoding = ENC_BASE64URL; break;
    case BE_BASE32: b->encoding = ENC_BASE32; break;
    case BE_BASE32HEX: b->encoding = ENC_BASE32HEX; break;
    case BE_BASE16: b->encoding = ENC_BASE16; break;
    case BE_BASE2MSBF: b->encoding = ENC_BASE2MSBF; break;
    case BE_BASE2LSBF: b->encoding = ENC_BASE2LSBF; break;
    case BE_Z85: b->encoding = ENC_Z85; break;
    case BE_DECODE: b->settings.decode = 1; break;
    case BE_IGNORE: b->settings.ignoreGarbage = 1; break;
    default: {
        char* end;

        b->settings.wrap = strtoll(value, &end, 10);
        if (*value == '\0' || *end != '\0' || b->settings.wrap < 0) {
            fprintf(stderr, "basenc: invalid wrap size: '%s'\n", value);
            return -1;
        }
    }
    }
    return 0;
}

static void encode_base16(const unsigned char* data, size_t length, long long wrap) {
    Wrapper w = { wrap, 0 };
    size_t i;

    for (i = 0; i < length; i++) {
        put_wrapped(&w, alphabet16[data[i] >> 4]);
        put_wrapped(&w, alphabet16[data[i] & 15]);
    }
    if (w.column > 0 && w.wrap > 0)
        putchar('\n');
}

static void encode_bits(const unsigned char* data, size_t length, long long wrap, int msbFirst) {
    Wrapper w = { wrap, 0 };
    size_t i;
    int k;

    for (i = 0; i < length; i++)
        for (k = 0; k < 8; k++)
            put_wrapped(&w, ((data[i] >> (msbFirst ? 7 - k : k)) & 1) ? '1' : '0');
    if (w.column > 0 && w.wrap > 0)
        putchar('\n');
}

/** Returns 0, or 1 when the length is not a multiple of 4 (nothing is written then). */
static int encode_z85(const unsigned char* data, size_t length, long long wrap) {
    Wrapper w = { wrap, 0 };
    size_t i;

    if (length % 4 != 0)
        return 1;
    for (i = 0; i < length; i += 4) {
        unsigned long value = ((unsigned long)data[i] << 24) | ((unsigned long)data[i + 1] << 16) |
                              ((unsigned long)data[i + 2] << 8) | data[i + 3];
        char chars[5];
        int k;

        for (k = 4; k >= 0; k--) {
            chars[k] = alphabetZ85[value % 85];
            value /= 85;
        }
        for (k = 0; k < 5; k++)
            put_wrapped(&w, chars[k]);
    }
    if (w.column > 0 && w.wrap > 0)
        putchar('\n');
    return 0;
}

/** Decodes base16, the bit strings and Z85, writing every complete unit before an error. Returns 0, or 1 for invalid input. */
static int decode_units(const unsigned char* data, size_t length, Encoding encoding, int ignoreGarbage) {
    unsigned long long accumulator = 0;
    int count = 0;
    size_t i;

    for (i = 0; i < length; i++) {
        unsigned char c = data[i];
        const char* at;
        int v;

        if (c == '\n')
            continue;
        switch (encoding) {
        case ENC_BASE16:
            at = c != '\0' ? strchr(alphabet16, c) : NULL;
            v = at != NULL ? (int)(at - alphabet16) : -1;
            break;
        case ENC_Z85:
            at = c != '\0' ? strchr(alphabetZ85, c) : NULL;
            v = at != NULL ? (int)(at - alphabetZ85) : -1;
            break;
        default:
            v = c == '0' ? 0 : (c == '1' ? 1 : -1);
            break;
        }
        if (v < 0) {
            if (ignoreGarbage && c != '=')                      // GNU takes = for the padding of a longer encoding
                continue;
            return 1;
        }
        if (encoding == ENC_BASE16) {
            accumulator = (accumulator << 4) | (unsigned)v;
            if (++count == 2) {
                putchar((int)accumulator);
                accumulator = 0;
                count = 0;
            }
        } else if (encoding == ENC_Z85) {
            accumulator = accumulator * 85 + (unsigned)v;
            if (++count == 5) {
                if (accumulator > 0xFFFFFFFFULL)
                    return 1;
                putchar((int)((accumulator >> 24) & 255));
                putchar((int)((accumulator >> 16) & 255));
                putchar((int)((accumulator >> 8) & 255));
                putchar((int)(accumulator & 255));
                accumulator = 0;
                count = 0;
            }
        } else {
            if (encoding == ENC_BASE2MSBF)
                accumulator = (accumulator << 1) | (unsigned)v;
            else
                accumulator |= (unsigned long long)v << count;
            if (++count == 8) {
                putchar((int)accumulator);
                accumulator = 0;
                count = 0;
            }
        }
    }
    return count != 0;
}

int basenc_main(int argc, char* argv[]) {
    ArgsSpec spec;
    BasencSettings b;
    const char** operands;
    int count, result, status = 0;
    FILE* in;
    unsigned char* data;
    size_t length;

    memset(&spec, 0, sizeof spec);
    spec.program = "basenc";
    spec.version = "0.1";
    spec.usage = print_basenc_usage;
    spec.options = basencOptions;
    spec.usageStatus = 1;
    memset(&b, 0, sizeof b);
    b.settings.wrap = 76;
    text_binary_streams();
    result = args_parse(&spec, argc, argv, on_basenc_option, &b, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (b.encoding == ENC_NONE) {
        fprintf(stderr, "basenc: missing encoding type\n");
        args_usage_error(&spec);
        return 1;
    }
    if (count > 1) {
        fprintf(stderr, "basenc: extra operand '%s'\n", operands[1]);
        args_usage_error(&spec);
        return 1;
    }
    if (text_open("basenc", count == 1 ? operands[0] : "-", 1, &in) != 0)
        return 1;
    data = text_slurp(in, &length);
    if (in != stdin)
        fclose(in);
    if (data == NULL) {
        fprintf(stderr, "basenc: read error\n");
        return 1;
    }

    if (b.settings.decode) {
        int bad;

        switch (b.encoding) {
        case ENC_BASE64: bad = decode(data, length, 64, alphabet64, b.settings.ignoreGarbage); break;
        case ENC_BASE64URL: bad = decode(data, length, 64, alphabet64url, b.settings.ignoreGarbage); break;
        case ENC_BASE32: bad = decode(data, length, 32, alphabet32, b.settings.ignoreGarbage); break;
        case ENC_BASE32HEX: bad = decode(data, length, 32, alphabet32hex, b.settings.ignoreGarbage); break;
        default: bad = decode_units(data, length, b.encoding, b.settings.ignoreGarbage); break;
        }
        if (bad) {
            fflush(stdout);
            fprintf(stderr, "basenc: invalid input\n");
            status = 1;
        }
    } else {
        switch (b.encoding) {
        case ENC_BASE64: encode(data, length, 64, alphabet64, b.settings.wrap); break;
        case ENC_BASE64URL: encode(data, length, 64, alphabet64url, b.settings.wrap); break;
        case ENC_BASE32: encode(data, length, 32, alphabet32, b.settings.wrap); break;
        case ENC_BASE32HEX: encode(data, length, 32, alphabet32hex, b.settings.wrap); break;
        case ENC_BASE16: encode_base16(data, length, b.settings.wrap); break;
        case ENC_BASE2MSBF: encode_bits(data, length, b.settings.wrap, 1); break;
        case ENC_BASE2LSBF: encode_bits(data, length, b.settings.wrap, 0); break;
        default:
            if (encode_z85(data, length, b.settings.wrap) != 0) {
                fprintf(stderr, "basenc: invalid input (length must be multiple of 4 characters)\n");
                status = 1;
            }
            break;
        }
    }
    free(data);
    return fflush(stdout) != 0 ? 1 : status;
}

