/*
 * printf: formats and prints its arguments, as GNU printf does.
 *
 * The FORMAT is used again and again until all the arguments are consumed (once when it has no
 * conversion). Missing arguments are an empty string or 0. Output is binary: only the bytes of the
 * format and of the arguments are written, never a CR.
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utf8.h"

#define VERSION "0.1"
#define PROGRAM "printf"

static int exitStatus = 0;

static void print_usage(void) {
    puts("Usage: printf FORMAT [ARGUMENT]...\n"
         "  or:  printf OPTION\n"
         "Print ARGUMENT(s) according to FORMAT.\n"
         "\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "FORMAT controls the output as in C printf. Interpreted sequences are:\n"
         "\n"
         "  \\\"      double quote\n"
         "  \\\\      backslash\n"
         "  \\a      alert (BEL)\n"
         "  \\b      backspace\n"
         "  \\c      produce no further output\n"
         "  \\e      escape\n"
         "  \\f      form feed\n"
         "  \\n      new line\n"
         "  \\r      carriage return\n"
         "  \\t      horizontal tab\n"
         "  \\v      vertical tab\n"
         "  \\NNN    byte with octal value NNN (1 to 3 digits)\n"
         "  \\xHH    byte with hexadecimal value HH (1 to 2 digits)\n"
         "  \\uHHHH  Unicode (ISO/IEC 10646) character with hex value HHHH (4 digits)\n"
         "  \\UHHHHHHHH  Unicode character with hex value HHHHHHHH (8 digits)\n"
         "  %%      a single %\n"
         "  %b      ARGUMENT as a string with '\\' escapes interpreted,\n"
         "          except that octal escapes are of the form \\0 or \\0NNN\n"
         "\n"
         "and all C format specifications ending with one of diouxXfeEgGaAcs, with\n"
         "ARGUMENTs converted to proper type first. Variable widths are handled.");
}

static void fail(const char* format, ...) {
    va_list ap;

    fflush(stdout);
    fprintf(stderr, PROGRAM ": ");
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void quit_ok(void) {
    fflush(stdout);
    exit(0);
}

static int hex_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int is_octal(int c) {
    return c >= '0' && c <= '7';
}

static void put_unicode(uint32_t value) {
    char bytes[4];
    size_t n;

    if (value > 0x10FFFF) {
        printf("\\U%08x", (unsigned)value);
        return;
    }
    n = utf8_encode(value, bytes);
    fwrite(bytes, 1, n, stdout);
}

/**
 * Prints the escape sequence at s, which points at the backslash. octal0 is for %b, where \0NNN is the
 * octal form (and \NNN is accepted too). Returns the number of bytes after the backslash that were used.
 */
static size_t print_escape(const char* s, int octal0) {
    const char* p = s + 1;
    int value = 0, length;

    if (*p == 'x') {
        for (length = 0, p++; length < 2 && hex_value((unsigned char)*p) >= 0; length++, p++)
            value = value * 16 + hex_value((unsigned char)*p);
        if (length == 0)
            fail("missing hexadecimal number in escape");
        putchar(value);
    } else if (is_octal((unsigned char)*p)) {
        if (octal0 && *p == '0')
            p++;
        for (length = 0; length < 3 && is_octal((unsigned char)*p); length++, p++)
            value = value * 8 + (*p - '0');
        putchar(value);
    } else if (*p != '\0' && strchr("\"\\abcefnrtv", *p) != NULL) {
        switch (*p++) {
            case 'a': putchar('\a'); break;
            case 'b': putchar('\b'); break;
            case 'c': quit_ok();
            case 'e': putchar(27);   break;
            case 'f': putchar('\f'); break;
            case 'n': putchar('\n'); break;
            case 'r': putchar('\r'); break;
            case 't': putchar('\t'); break;
            case 'v': putchar('\v'); break;
            case '"': putchar('"');  break;
            default:  putchar('\\'); break;
        }
    } else if (*p == 'u' || *p == 'U') {
        char kind = *p++;
        uint32_t code = 0;

        for (length = kind == 'u' ? 4 : 8; length > 0; length--, p++) {
            if (hex_value((unsigned char)*p) < 0)
                fail("missing hexadecimal number in escape");
            code = code * 16 + (uint32_t)hex_value((unsigned char)*p);
        }
        if ((code <= 0x9f && code != 0x24 && code != 0x40 && code != 0x60) || (code >= 0xd800 && code <= 0xdfff))
            fail("invalid universal character name \\%c%0*x", kind, kind == 'u' ? 4 : 8, (unsigned)code);
        put_unicode(code);
    } else {
        putchar('\\');
        if (*p != '\0') {
            putchar(*p);
            p++;
        }
    }
    return (size_t)(p - s - 1);
}

/** The %b conversion: the argument with its escapes interpreted. */
static void print_escaped_argument(const char* s) {
    for (; *s != '\0'; s++) {
        if (*s == '\\')
            s += print_escape(s, 1);
        else
            putchar(*s);
    }
}

/**
 * Checks how much of a number was converted, as GNU printf does: nothing converted or something left
 * over is an error message and exit status 1, but the value is still used.
 */
static void verify_numeric(const char* text, const char* end, int error) {
    if (error != 0) {
        fprintf(stderr, PROGRAM ": %s: %s\n", text, error == ERANGE ? "Numerical result out of range" : strerror(error));
        exitStatus = 1;
    } else if (*end != '\0') {
        if (end == text)
            fprintf(stderr, PROGRAM ": %s: expected a numeric value\n", text);
        else
            fprintf(stderr, PROGRAM ": %s: value not completely converted\n", text);
        exitStatus = 1;
    }
}

/** A leading ' or " makes the value the character code of the next character. */
static int character_constant(const char* text, uintmax_t* value) {
    size_t length;
    const char* rest;

    if (text[0] != '"' && text[0] != '\'')
        return 0;
    if (text[1] == '\0')
        return 0;   // a lone quote is not a number
    *value = utf8_decode((const unsigned char*)text + 1, strlen(text + 1), &length);
    if (*value >= UTF8_INVALID_BASE)
        *value = (unsigned char)text[1];
    rest = text + 1 + length;
    if (*rest != '\0') {
        fprintf(stderr, PROGRAM ": warning: %s: character(s) following character constant have been ignored\n", rest);
    }
    return 1;
}

static intmax_t to_signed(const char* text) {
    uintmax_t constant;
    char* end;
    intmax_t value;

    if (character_constant(text, &constant))
        return (intmax_t)constant;
    errno = 0;
    value = strtoimax(text, &end, 0);
    verify_numeric(text, end, errno);
    return value;
}

static uintmax_t to_unsigned(const char* text) {
    uintmax_t constant;
    char* end;
    uintmax_t value;

    if (character_constant(text, &constant))
        return constant;
    errno = 0;
    value = strtoumax(text, &end, 0);
    verify_numeric(text, end, errno);
    return value;
}

static double to_float(const char* text) {
    uintmax_t constant;
    char* end;
    double value;

    if (character_constant(text, &constant))
        return (double)constant;
    errno = 0;
    value = strtod(text, &end);
    verify_numeric(text, end, errno);
    return value;
}

typedef struct {
    int argc;
    char** argv;
    int used;
} Arguments;

static const char* next_argument(Arguments* a) {
    if (a->used < a->argc)
        return a->argv[a->used++];
    return "";
}

/** Writes vsnprintf's result whatever its length, with every byte (the %c of a NUL included). */
static void emit(const char* spec, ...) {
    char small[256];
    char* big = NULL;
    va_list ap;
    int length;

    va_start(ap, spec);
    length = vsnprintf(small, sizeof small, spec, ap);
    va_end(ap);
    if (length < 0)
        return;
    if ((size_t)length < sizeof small) {
        fwrite(small, 1, (size_t)length, stdout);
        return;
    }
    big = malloc((size_t)length + 1);
    if (big == NULL)
        fail("memory exhausted");
    va_start(ap, spec);
    vsnprintf(big, (size_t)length + 1, spec, ap);
    va_end(ap);
    fwrite(big, 1, (size_t)length, stdout);
    free(big);
}

/**
 * %a and %A. The C library of MSVC always writes 13 hexadecimal digits, glibc writes only the ones
 * that are needed (0x1p+0), so the digits are made here. A subnormal number is normalized, as the
 * long double of the GNU tools has it.
 */
static void print_hex_float(double value, int upper, int precision, const char* flags, int width) {
    char text[64];
    char sign[2] = "";
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int left = strchr(flags, '-') != NULL, zero = strchr(flags, '0') != NULL, alt = strchr(flags, '#') != NULL;
    uint64_t bits, frac;
    int exponent, lead = 1, n, length, pad;
    char body[48];

    memcpy(&bits, &value, sizeof bits);
    if (bits >> 63)
        sign[0] = '-';
    else if (strchr(flags, '+') != NULL)
        sign[0] = '+';
    else if (strchr(flags, ' ') != NULL)
        sign[0] = ' ';
    exponent = (int)((bits >> 52) & 0x7ff);
    frac = bits & ((1ull << 52) - 1);

    if (exponent == 0x7ff) {
        const char* word = frac != 0 ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        length = sprintf(text, "%s%s", sign, word);
        zero = 0;
        pad = width > length ? width - length : 0;
        if (!left)
            for (n = 0; n < pad; n++) putchar(' ');
        fwrite(text, 1, (size_t)length, stdout);
        if (left)
            for (n = 0; n < pad; n++) putchar(' ');
        return;
    }

    if (exponent == 0 && frac == 0) {
        lead = 0;
        exponent = 0;
    } else if (exponent == 0) {
        int h = 51;
        while (!((frac >> h) & 1))
            h--;
        exponent = h - 1074;
        frac = (frac << (52 - h)) & ((1ull << 52) - 1);
    } else {
        exponent -= 1023;
    }

    if (precision >= 0 && precision < 13) {
        int shift = (13 - precision) * 4;
        uint64_t keep = frac >> shift, rest = frac & ((1ull << shift) - 1), half = 1ull << (shift - 1);
        int odd = precision == 0 ? lead & 1 : (int)(keep & 1);

        if (rest > half || (rest == half && odd))
            keep++;
        if (precision == 0) {
            lead += (int)keep;
            frac = 0;
        } else if (keep >> (precision * 4)) {
            lead++;
            keep &= (1ull << (precision * 4)) - 1;
            frac = keep << shift;
        } else {
            frac = keep << shift;
        }
    }

    // the digits after the point
    {
        char fraction[16];
        int count = 13;
        for (n = 0; n < 13; n++)
            fraction[n] = digits[(frac >> (48 - 4 * n)) & 15];
        if (precision < 0) {
            while (count > 0 && fraction[count - 1] == '0')
                count--;
        } else if (precision < 13) {
            count = precision;
        }
        length = sprintf(body, "%c", digits[lead]);
        if (count > 0 || precision > 13 || alt)
            body[length++] = '.';
        memcpy(body + length, fraction, (size_t)count);
        length += count;
        for (n = 13; n < precision; n++)
            body[length++] = '0';
        length += sprintf(body + length, "%c%c%d", upper ? 'P' : 'p', exponent < 0 ? '-' : '+', exponent < 0 ? -exponent : exponent);
    }

    // sign, 0x, [zeros], body
    length = (int)strlen(sign) + 2 + length;
    pad = width > length ? width - length : 0;
    if (!left && !zero)
        for (n = 0; n < pad; n++) putchar(' ');
    fputs(sign, stdout);
    fputs(upper ? "0X" : "0x", stdout);
    if (!left && zero)
        for (n = 0; n < pad; n++) putchar('0');
    fputs(body, stdout);
    if (left)
        for (n = 0; n < pad; n++) putchar(' ');
}

/**
 * Handles the conversion that starts at f (the '%'). Returns the number of bytes of the format used.
 */
static size_t print_directive(const char* f, Arguments* args) {
    const char* p = f + 1;
    char flags[8];
    size_t flagCount = 0;
    int width = 0, precision = -1;
    char spec[32];
    int conversion;

    // flags (the ' of glibc is accepted and does nothing)
    while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0' || *p == '\'') {
        if (*p != '\'' && flagCount + 1 < sizeof flags && memchr(flags, *p, flagCount) == NULL)
            flags[flagCount++] = *p;
        p++;
    }
    flags[flagCount] = '\0';

    if (*p == '*') {
        p++;
        width = (int)to_signed(next_argument(args));
    } else {
        long w = 0;
        while (*p >= '0' && *p <= '9') {
            w = w * 10 + (*p++ - '0');
            if (w > 100000000L)
                w = 100000000L;
        }
        width = (int)w;
    }

    if (*p == '.') {
        p++;
        if (*p == '*') {
            p++;
            precision = (int)to_signed(next_argument(args));
        } else {
            long v = 0;
            while (*p >= '0' && *p <= '9') {
                v = v * 10 + (*p++ - '0');
                if (v > 100000000L)
                    v = 100000000L;
            }
            precision = (int)v;
        }
    }

    // length modifiers are not needed: the conversions use the widest type
    while (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'j' || *p == 'z' || *p == 't' || *p == 'q')
        p++;

    conversion = (unsigned char)*p;
    switch (conversion) {
        case 'd': case 'i': {
            intmax_t value = to_signed(next_argument(args));
            const char* keep = "-+ 0#";
            char kept[8];
            size_t n = 0, i;
            for (i = 0; flags[i] != '\0'; i++)
                if (strchr(keep, flags[i]) != NULL && flags[i] != '#')
                    kept[n++] = flags[i];
            kept[n] = '\0';
            sprintf(spec, "%%%s*.*" "jd", kept);
            emit(spec, width, precision, value);
            break;
        }
        case 'o': case 'u': case 'x': case 'X': {
            uintmax_t value = to_unsigned(next_argument(args));
            char kept[8];
            size_t n = 0, i;
            for (i = 0; flags[i] != '\0'; i++) {
                if (flags[i] == '-' || flags[i] == '0' || (flags[i] == '#' && conversion != 'u'))
                    kept[n++] = flags[i];
            }
            kept[n] = '\0';
            sprintf(spec, "%%%s*.*j%c", kept, conversion);
            emit(spec, width, precision, value);
            break;
        }
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
            double value = to_float(next_argument(args));
            char kept[8];
            size_t n = 0, i;

            if (conversion == 'a' || conversion == 'A') {
                char own[16];
                if (width < 0) {
                    width = -width;
                    sprintf(own, "%s-", flags);
                } else {
                    strcpy(own, flags);
                }
                print_hex_float(value, conversion == 'A', precision, own, width);
                break;
            }
            for (i = 0; flags[i] != '\0'; i++)
                kept[n++] = flags[i];
            kept[n] = '\0';
            sprintf(spec, "%%%s*.*%c", kept, conversion);
            emit(spec, width, precision, value);
            break;
        }
        case 's': {
            const char* value = next_argument(args);
            sprintf(spec, "%%%s*.*s", strchr(flags, '-') != NULL ? "-" : "");
            emit(spec, width, precision, value);
            break;
        }
        case 'c': {
            const char* value = next_argument(args);
            sprintf(spec, "%%%s*c", strchr(flags, '-') != NULL ? "-" : "");
            emit(spec, width, (int)(unsigned char)value[0]);
            break;
        }
        case 'b':
            print_escaped_argument(next_argument(args));
            break;
        default:
            fail("%.*s: invalid conversion specification", (int)(p - f + (*p != '\0')), f);
    }
    return (size_t)(p - f);
}

/** One pass over the format. Returns the number of arguments consumed. */
static int print_format(const char* format, int argc, char** argv) {
    Arguments args;
    const char* f;

    args.argc = argc;
    args.argv = argv;
    args.used = 0;

    for (f = format; *f != '\0'; f++) {
        if (*f == '%' && f[1] == '%') {
            putchar('%');
            f++;
        } else if (*f == '%') {
            f += print_directive(f, &args);
        } else if (*f == '\\') {
            f += print_escape(f, 0);
        } else {
            putchar(*f);
        }
    }
    return args.used;
}

int main(int argc, char* argv[]) {
    const char* format;
    int used;

    _setmode(_fileno(stdout), _O_BINARY);

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts(PROGRAM " (Winshell) " VERSION);
        return 0;
    }

    argv++, argc--;
    if (argc > 0 && strcmp(argv[0], "--") == 0)
        argv++, argc--;
    if (argc <= 0) {
        fprintf(stderr, PROGRAM ": missing operand\nTry '" PROGRAM " --help' for more information.\n");
        return 1;
    }

    format = argv[0];
    argv++, argc--;

    do {
        used = print_format(format, argc, argv);
        argc -= used;
        argv += used;
    } while (used > 0 && argc > 0);

    if (argc > 0)
        fprintf(stderr, PROGRAM ": warning: ignoring excess arguments, starting with '%s'\n", argv[0]);

    fflush(stdout);
    return exitStatus;
}
