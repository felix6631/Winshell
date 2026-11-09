/*
 * numfmt: reformats numbers, scaling them with the suffixes of SI (K = 1000) or IEC (K = 1024, Ki), as GNU numfmt does:
 * the numbers of the fields of lines (standard input) or of the operands, with --from / --to scaling, --round, --padding,
 * --format, --suffix, --field, --delimiter, --header, --invalid. GNU computes with long double; here the values are
 * exact fractions of big integers, so a result is what the exact value gives (GNU can differ in the last place for
 * numbers with 16 or more digits).
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "textutil.h"

#define VERSION "0.1"
#define PROGRAM "numfmt"

typedef unsigned long long u64;

static void print_usage(void) {
    puts("Usage: numfmt [OPTION]... [NUMBER]...\n"
         "Reformat NUMBER(s), or the numbers from standard input if none are specified.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "      --debug          print warnings about invalid input\n"
         "  -d, --delimiter=X    use X instead of whitespace for field delimiter\n"
         "      --field=FIELDS   replace the numbers in these input fields (default=1)\n"
         "                         see FIELDS below\n"
         "      --format=FORMAT  use printf style floating-point FORMAT;\n"
         "                         see FORMAT below for details\n"
         "      --from=UNIT      auto-scale input numbers to UNITs; default is 'none';\n"
         "                         see UNIT below\n"
         "      --from-unit=N    specify the input unit size (instead of the default 1)\n"
         "      --grouping       use locale-defined grouping of digits, e.g. 1,000,000\n"
         "                         (which means it has no effect in the C/POSIX locale)\n"
         "      --header[=N]     print (without converting) the first N header lines;\n"
         "                         N defaults to 1 if not specified\n"
         "      --invalid=MODE   failure mode for invalid numbers: MODE can be:\n"
         "                         abort (default), fail, warn, ignore\n"
         "      --padding=N      pad the output to N characters; positive N will\n"
         "                         right-align; negative N will left-align;\n"
         "                         padding is ignored if the output is wider than N;\n"
         "                         the default is to automatically pad if a whitespace\n"
         "                         is found\n"
         "      --round=METHOD   use METHOD for rounding when scaling; METHOD can be:\n"
         "                         up, down, from-zero (default), towards-zero, nearest\n"
         "      --suffix=SUFFIX  add SUFFIX to output numbers, and accept optional\n"
         "                         SUFFIX in input numbers\n"
         "      --to=UNIT        auto-scale output numbers to UNITs; see UNIT below\n"
         "      --to-unit=N      the output unit size (instead of the default 1)\n"
         "  -z, --zero-terminated    line delimiter is NUL, not newline\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "UNIT options:\n"
         "  none       no auto-scaling is done; suffixes will trigger an error\n"
         "  auto       accept optional single/two letter suffix:\n"
         "               1K = 1000,\n"
         "               1Ki = 1024,\n"
         "               1M = 1000000,\n"
         "               1Mi = 1048576,\n"
         "  si         accept optional single letter suffix:\n"
         "               1K = 1000,\n"
         "               1M = 1000000,\n"
         "               ...\n"
         "  iec        accept optional single letter suffix:\n"
         "               1K = 1024,\n"
         "               1M = 1048576,\n"
         "               ...\n"
         "  iec-i      accept optional two-letter suffix:\n"
         "               1Ki = 1024,\n"
         "               1Mi = 1048576,\n"
         "               ...\n"
         "\n"
         "FIELDS supports cut(1) style field ranges:\n"
         "  N    N'th field, counted from 1\n"
         "  N-   from N'th field, to end of line\n"
         "  N-M  from N'th to M'th field (inclusive)\n"
         "  -M   from first to M'th field (inclusive)\n"
         "  -    all fields\n"
         "Multiple fields/ranges can be separated with commas\n"
         "\n"
         "FORMAT must be suitable for printing one floating-point argument '%f'.\n"
         "Optional quote (%'f) will enable --grouping (if supported by current locale).\n"
         "Optional width value (%10f) will pad output. Optional zero (%010f) width\n"
         "will zero pad the number. Optional negative values (%-10f) will left align.\n"
         "Optional precision (%.1f) will override the input determined precision.\n"
         "\n"
         "Exit status is 0 if all input numbers were successfully converted.\n"
         "By default, numfmt will stop at the first conversion error with exit status 2.\n"
         "With --invalid='fail' a warning is printed for each conversion error\n"
         "and the exit status is 2.  With --invalid='warn' each conversion error is\n"
         "diagnosed, but the exit status is 0.  With --invalid='ignore' conversion\n"
         "errors are not diagnosed and the exit status is 0.\n"
         "\n"
         "Examples:\n"
         "  $ numfmt --to=si 1000\n"
         "            -> \"1.0K\"\n"
         "  $ numfmt --to=iec 2048\n"
         "           -> \"2.0K\"\n"
         "  $ numfmt --to=iec-i 4096\n"
         "           -> \"4.0Ki\"\n"
         "  $ echo 1K | numfmt --from=si\n"
         "           -> \"1000\"\n"
         "  $ echo 1K | numfmt --from=iec\n"
         "           -> \"1024\"\n"
         "  $ df -B1 | numfmt --header --field 2-4 --to=si\n"
         "  $ ls -l  | numfmt --header --field 5 --to=iec\n"
         "  $ ls -lh | numfmt --header --field 5 --from=iec --padding=10\n"
         "  $ ls -lh | numfmt --header --field 5 --from=iec --format %10f");
}

enum {
    OPT_DEBUG = 1, OPT_DELIMITER, OPT_FIELD, OPT_FORMAT, OPT_FROM, OPT_FROM_UNIT, OPT_GROUPING, OPT_HEADER, OPT_INVALID,
    OPT_PADDING, OPT_ROUND, OPT_SUFFIX, OPT_TO, OPT_TO_UNIT, OPT_ZERO
};

static const ArgsOption options[] = {
    { OPT_DEBUG,     0,   "debug",           ARGS_NONE },
    { OPT_DELIMITER, 'd', "delimiter",       ARGS_REQUIRED },
    { OPT_FIELD,     0,   "field",           ARGS_REQUIRED },
    { OPT_FORMAT,    0,   "format",          ARGS_REQUIRED },
    { OPT_FROM,      0,   "from",            ARGS_REQUIRED },
    { OPT_FROM_UNIT, 0,   "from-unit",       ARGS_REQUIRED },
    { OPT_GROUPING,  0,   "grouping",        ARGS_NONE },
    { OPT_HEADER,    0,   "header",          ARGS_OPTIONAL },
    { OPT_INVALID,   0,   "invalid",         ARGS_REQUIRED },
    { OPT_PADDING,   0,   "padding",         ARGS_REQUIRED },
    { OPT_ROUND,     0,   "round",           ARGS_REQUIRED },
    { OPT_SUFFIX,    0,   "suffix",          ARGS_REQUIRED },
    { OPT_TO,        0,   "to",              ARGS_REQUIRED },
    { OPT_TO_UNIT,   0,   "to-unit",         ARGS_REQUIRED },
    { OPT_ZERO,      'z', "zero-terminated", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

/* ---- big integers: just enough for the exact arithmetic ---- */

#define LIMBS 40

typedef struct {
    unsigned int d[LIMBS];
    int          n;                 /* used limbs; 0 is zero */
} Big;

static void big_trim(Big* a) {
    while (a->n > 0 && a->d[a->n - 1] == 0)
        a->n--;
}

static void big_set(Big* a, u64 v) {
    a->n = 0;
    while (v != 0) {
        a->d[a->n++] = (unsigned int)(v & 0xFFFFFFFFu);
        v >>= 32;
    }
}

static void big_mul_small(Big* a, unsigned int m) {
    u64 carry = 0;
    int i;

    for (i = 0; i < a->n; i++) {
        u64 t = (u64)a->d[i] * m + carry;

        a->d[i] = (unsigned int)(t & 0xFFFFFFFFu);
        carry = t >> 32;
    }
    if (carry != 0 && a->n < LIMBS)
        a->d[a->n++] = (unsigned int)carry;
}

static void big_mul_u64(Big* a, u64 m) {
    Big copy = *a;
    int i;

    big_mul_small(a, (unsigned int)(m & 0xFFFFFFFFu));
    if ((m >> 32) != 0) {
        big_mul_small(&copy, (unsigned int)(m >> 32));
        // add copy shifted by one limb
        {
            u64 carry = 0;
            int len = copy.n + 1 > a->n ? copy.n + 1 : a->n;

            for (i = 0; i < len && i < LIMBS; i++) {
                u64 x = (u64)(i < a->n ? a->d[i] : 0u) + carry;

                if (i >= 1 && i - 1 < copy.n)
                    x += copy.d[i - 1];
                a->d[i] = (unsigned int)(x & 0xFFFFFFFFu);
                carry = x >> 32;
            }
            a->n = len < LIMBS ? len : LIMBS;
            big_trim(a);
        }
    }
}

static unsigned int big_divmod_small(Big* a, unsigned int m) {
    u64 rem = 0;
    int i;

    for (i = a->n - 1; i >= 0; i--) {
        u64 t = (rem << 32) | a->d[i];

        a->d[i] = (unsigned int)(t / m);
        rem = t % m;
    }
    big_trim(a);
    return (unsigned int)rem;
}

static int big_cmp(const Big* a, const Big* b) {
    int i;

    if (a->n != b->n)
        return a->n < b->n ? -1 : 1;
    for (i = a->n - 1; i >= 0; i--) {
        if (a->d[i] != b->d[i])
            return a->d[i] < b->d[i] ? -1 : 1;
    }
    return 0;
}

static void big_add(Big* a, const Big* b) {
    u64 carry = 0;
    int i, len = a->n > b->n ? a->n : b->n;

    for (i = 0; i < len || carry != 0; i++) {
        u64 x = (u64)(i < a->n ? a->d[i] : 0u) + (i < b->n ? b->d[i] : 0u) + carry;

        if (i >= LIMBS)
            break;
        a->d[i] = (unsigned int)(x & 0xFFFFFFFFu);
        carry = x >> 32;
        if (i + 1 > a->n)
            a->n = i + 1;
    }
}

/** a -= b, a >= b. */
static void big_sub(Big* a, const Big* b) {
    long long borrow = 0;
    int i;

    for (i = 0; i < a->n; i++) {
        long long x = (long long)a->d[i] - (i < b->n ? b->d[i] : 0u) - borrow;

        if (x < 0) {
            x += 0x100000000LL;
            borrow = 1;
        } else {
            borrow = 0;
        }
        a->d[i] = (unsigned int)x;
    }
    big_trim(a);
}

static void big_shl1(Big* a) {
    unsigned int carry = 0;
    int i;

    for (i = 0; i < a->n; i++) {
        unsigned int next = a->d[i] >> 31;

        a->d[i] = (a->d[i] << 1) | carry;
        carry = next;
    }
    if (carry != 0 && a->n < LIMBS)
        a->d[a->n++] = carry;
}

static int big_bits(const Big* a) {
    int bits;
    unsigned int top;

    if (a->n == 0)
        return 0;
    top = a->d[a->n - 1];
    bits = (a->n - 1) * 32;
    while (top != 0) {
        bits++;
        top >>= 1;
    }
    return bits;
}

/** q = a / b, r = a % b (binary long division). */
static void big_divmod(const Big* a, const Big* b, Big* q, Big* r) {
    int bit;

    q->n = 0;
    r->n = 0;
    for (bit = big_bits(a) - 1; bit >= 0; bit--) {
        big_shl1(r);
        if ((a->d[bit / 32] >> (bit % 32)) & 1u) {
            if (r->n == 0)
                r->n = 1, r->d[0] = 0;
            r->d[0] |= 1u;
        }
        big_trim(r);
        big_shl1(q);
        if (big_cmp(r, b) >= 0) {
            big_sub(r, b);
            if (q->n == 0)
                q->n = 1, q->d[0] = 0;
            q->d[0] |= 1u;
        }
    }
    big_trim(q);
}

static void big_pow10(Big* a, int n) {
    big_set(a, 1);
    while (n-- > 0)
        big_mul_small(a, 10);
}

static void big_to_dec(const Big* a, char* out, size_t size) {
    Big t = *a;
    char tmp[LIMBS * 10 + 2];
    int n = 0, i;

    if (t.n == 0) {
        snprintf(out, size, "0");
        return;
    }
    while (t.n > 0)
        tmp[n++] = (char)('0' + big_divmod_small(&t, 10));
    for (i = 0; i < n && (size_t)i + 1 < size; i++)
        out[i] = tmp[n - 1 - i];
    out[i] = '\0';
}

static double big_to_double(const Big* a) {
    double v = 0;
    int i;

    for (i = a->n - 1; i >= 0; i--)
        v = v * 4294967296.0 + a->d[i];
    return v;
}

/* ---- the settings ---- */

typedef enum { SCALE_NONE, SCALE_AUTO, SCALE_SI, SCALE_IEC, SCALE_IEC_I } Scale;
typedef enum { ROUND_UP, ROUND_DOWN, ROUND_FROM_ZERO, ROUND_TOWARDS_ZERO, ROUND_NEAREST } Round;
typedef enum { INVALID_ABORT, INVALID_FAIL, INVALID_WARN, INVALID_IGNORE } Invalid;

static Scale scaleFrom = SCALE_NONE, scaleTo = SCALE_NONE;
static Round roundStyle = ROUND_FROM_ZERO;
static Invalid invalidStyle = INVALID_ABORT;
static const char* suffix;
static u64 fromUnit = 1, toUnit = 1;
static int grouping, groupingOption;
static const char* pendingFormat;
static long paddingWidth;
static long zeroPaddingWidth;
static long userPrecision = -1;
static const char* formatStr;
static char* formatPrefix;
static char* formatSuffix;
static int autoPadding;
static unsigned long header;
static int debug;
static int delimiter = -1;          /* -1: blanks */
static int lineDelimiter = '\n';
static int convExitCode = 2;
static int validNumbers = 1;
static int headerGiven;

typedef struct { unsigned long first, last; } Range;
static Range* fieldRanges;
static size_t fieldRangeCount;

/** The text as GNU quote() writes it. */
static const char* quote(const char* text) {
    static char buffer[4][2048];
    static int which;
    char* out = buffer[which++ & 3];
    size_t n = 0;
    const unsigned char* p;

    out[n++] = '\'';
    for (p = (const unsigned char*)text; *p != '\0' && n < 2048 - 8; p++) {
        switch (*p) {
        case '\\': out[n++] = '\\'; out[n++] = '\\'; break;
        case '\'': out[n++] = '\\'; out[n++] = '\''; break;
        case '\n': out[n++] = '\\'; out[n++] = 'n'; break;
        case '\t': out[n++] = '\\'; out[n++] = 't'; break;
        case '\r': out[n++] = '\\'; out[n++] = 'r'; break;
        default:
            if (*p < 32 || *p == 127)
                n += (size_t)snprintf(out + n, 8, "\\%03o", *p);
            else
                out[n++] = (char)*p;
        }
    }
    out[n++] = '\'';
    out[n] = '\0';
    return out;
}

static void die(const char* format, const char* argument) {
    fflush(stdout);
    fprintf(stderr, PROGRAM ": ");
    fprintf(stderr, format, argument);
    fputc('\n', stderr);
    exit(1);
}

static void usage_die(const char* format, const char* argument) {
    fflush(stdout);
    fprintf(stderr, PROGRAM ": ");
    fprintf(stderr, format, argument);
    fputc('\n', stderr);
    args_usage_error(&spec);
    exit(1);
}

/** error (status, 0, ...): the message; exits at once when status is not 0. */
static void conv_error(const char* format, const char* a, const char* b) {
    if (invalidStyle == INVALID_IGNORE)
        return;
    fflush(stdout);
    fprintf(stderr, PROGRAM ": ");
    fprintf(stderr, format, a, b);
    fputc('\n', stderr);
    if (convExitCode != 0)
        exit(convExitCode);
}

/* ---- argument matching (argmatch) ---- */

static int argmatch(const char* option, const char* value, const char* const* names, int count) {
    size_t length = strlen(value);
    int i, found = -1, ambiguous = 0;

    for (i = 0; i < count; i++) {
        if (strncmp(names[i], value, length) == 0) {
            if (strlen(names[i]) == length)
                return i;
            if (found < 0)
                found = i;
            else
                ambiguous = 1;
        }
    }
    if (found >= 0 && !ambiguous)
        return found;
    fflush(stdout);
    fprintf(stderr, PROGRAM ": %s argument %s for '--%s'\nValid arguments are:\n", ambiguous ? "ambiguous" : "invalid", quote(value), option);
    for (i = 0; i < count; i++)
        fprintf(stderr, "  - '%s'\n", names[i]);
    args_usage_error(&spec);
    exit(1);
}

/* ---- the numbers ---- */

typedef struct {
    Big          num, den;
    int          neg;
    unsigned int precision;
} Value;

typedef enum {
    SSE_OK, SSE_OVERFLOW, SSE_INVALID_NUMBER, SSE_FORBIDDEN_SUFFIX, SSE_INVALID_SUFFIX, SSE_MISSING_I
} Sse;

static int valid_suffix(int c) {
    return c != 0 && strchr("KMGTPEZY", c) != NULL;
}

static int suffix_power(int c) {
    return (int)(strchr("KMGTPEZY", c) - "KMGTPEZY") + 1;
}

#define MAX_INT_DIGITS 27

/** Reads the number at *p (digits with an optional fraction) into v; leaves *p after it. */
static Sse parse_number(const char** p, Value* v) {
    const char* s = *p;
    int intDigits = 0, fracDigits = 0;
    int haveFraction = 0, sawZero = 0;

    big_set(&v->num, 0);
    big_set(&v->den, 1);
    v->neg = 0;
    v->precision = 0;
    if (*s == '-') {
        v->neg = 1;
        s++;
    }
    while (isdigit((unsigned char)*s)) {
        if (*s != '0' || intDigits > 0 || 1)
            ;
        big_mul_small(&v->num, 10);
        {
            Big d;

            big_set(&d, (u64)(*s - '0'));
            big_add(&v->num, &d);
        }
        if (intDigits > 0 || *s != '0')
            intDigits++;
        else
            sawZero = 1;
        s++;
    }
    if (*s == '.') {
        const char* q = s + 1;

        if (!isdigit((unsigned char)*q)) {
            return SSE_INVALID_NUMBER;
        }
        haveFraction = 1;
        while (isdigit((unsigned char)*q)) {
            if (fracDigits < 80) {
                Big d;

                big_mul_small(&v->num, 10);
                big_mul_small(&v->den, 10);
                big_set(&d, (u64)(*q - '0'));
                big_add(&v->num, &d);
            }
            fracDigits++;
            q++;
        }
        s = q;
    }
    if (intDigits == 0 && !haveFraction && !sawZero)
        return SSE_INVALID_NUMBER;
    if (intDigits > MAX_INT_DIGITS || fracDigits > 27)
        return SSE_OVERFLOW;
    v->precision = (unsigned int)fracDigits;
    *p = s;
    return SSE_OK;
}

/** The number with its suffix, scaled as the --from setting says. */
static Sse parse_human(const char* text, const char** end, Value* v, Scale allowed) {
    const char* s = text;
    Sse e = parse_number(&s, v);
    unsigned int base = allowed == SCALE_IEC || allowed == SCALE_IEC_I ? 1024 : 1000;

    *end = s;
    if (e != SSE_OK)
        return e;
    if (*s != '\0') {
        int power, i;

        while (*s == ' ' || *s == '\t')
            s++;
        if (!valid_suffix((unsigned char)*s))
            return SSE_INVALID_SUFFIX;
        if (allowed == SCALE_NONE)
            return SSE_FORBIDDEN_SUFFIX;
        power = suffix_power((unsigned char)*s);
        s++;
        if (allowed == SCALE_AUTO && *s == 'i') {
            base = 1024;
            s++;
        } else if (allowed == SCALE_IEC_I) {
            if (*s == 'i')
                s++;
            else
                return SSE_MISSING_I;
        }
        v->precision = 0;
        for (i = 0; i < power; i++)
            big_mul_small(&v->num, base);
        *end = s;
    } else if (allowed == SCALE_IEC_I) {
        return SSE_MISSING_I;
    }
    return SSE_OK;
}

/* ---- the output of a number ---- */

static double value_to_double(const Value* v) {
    double n = big_to_double(&v->num), d = big_to_double(&v->den);

    return v->neg ? -n / d : n / d;
}

/** The decimal digits of |value| * 10^digits rounded as the style says to an integer string; the sign is the caller's. */
static void round_to_digits(const Big* num, const Big* den, int neg, int digits, Round style, int halfEven, Big* out) {
    Big n = *num, q, r, twice;
    int i, up = 0;

    for (i = 0; i < digits; i++)
        big_mul_small(&n, 10);
    big_divmod(&n, den, &q, &r);
    if (r.n != 0) {
        twice = r;
        big_shl1(&twice);
        if (halfEven) {
            int c = big_cmp(&twice, den);

            up = c > 0 || (c == 0 && q.n > 0 && (q.d[0] & 1u));
        } else {
            switch (style) {
            case ROUND_FROM_ZERO: up = 1; break;
            case ROUND_TOWARDS_ZERO: up = 0; break;
            case ROUND_UP: up = !neg; break;
            case ROUND_DOWN: up = neg; break;
            case ROUND_NEAREST: up = big_cmp(&twice, den) >= 0; break;
            }
        }
    }
    if (up) {
        Big one;

        big_set(&one, 1);
        big_add(&q, &one);
    }
    *out = q;
}

/** Prints q / 10^digits with a sign, zero padded to the width (the sign and the point included). */
static void format_decimal(const Big* q, int digits, int neg, char* out, size_t size) {
    char text[LIMBS * 10 + 8], result[LIMBS * 10 + 64];
    size_t length, pos = 0;
    int i;

    big_to_dec(q, text, sizeof text);
    length = strlen(text);
    if (neg)
        result[pos++] = '-';
    if (digits == 0) {
        memcpy(result + pos, text, length);
        pos += length;
    } else {
        if ((int)length <= digits) {
            result[pos++] = '0';
            result[pos++] = '.';
            for (i = 0; i < digits - (int)length; i++)
                result[pos++] = '0';
            memcpy(result + pos, text, length);
            pos += length;
        } else {
            memcpy(result + pos, text, length - (size_t)digits);
            pos += length - (size_t)digits;
            result[pos++] = '.';
            memcpy(result + pos, text + length - digits, (size_t)digits);
            pos += (size_t)digits;
        }
    }
    result[pos] = '\0';
    if (zeroPaddingWidth > 0 && (long)pos < zeroPaddingWidth) {
        size_t missing = (size_t)zeroPaddingWidth - pos, signLength = neg ? 1 : 0;
        char padded[LIMBS * 10 + 128];

        memcpy(padded, result, signLength);
        memset(padded + signLength, '0', missing);
        memcpy(padded + signLength + missing, result + signLength, pos - signLength + 1);
        snprintf(out, size, "%s", padded);
        return;
    }
    snprintf(out, size, "%s", result);
}

static const char* const suffixLetters = "KMGTPEZY";

/** Makes the text of the number (with the scale suffix); returns 0 when it cannot be printed. */
static int make_number(Value* v, char* out, size_t size) {
    Big den = v->den, q, one;
    double approx = 0;
    unsigned int x;
    char dec[LIMBS * 10 + 8];
    int digits;

    // the value in units: num / (den * toUnit), times fromUnit
    {
        Big num = v->num;

        big_mul_u64(&num, fromUnit);
        big_mul_u64(&den, toUnit);
        v->num = num;
    }
    approx = big_to_double(&v->num) / big_to_double(&den);
    {
        Big integer, rest;

        big_divmod(&v->num, &den, &integer, &rest);
        big_to_dec(&integer, dec, sizeof dec);
        x = integer.n == 0 ? 0 : (unsigned int)strlen(dec) - 1;
    }
    if (scaleTo == SCALE_NONE && x + v->precision > 18) {
        if (invalidStyle != INVALID_IGNORE) {
            char value[64], precision[32];

            snprintf(value, sizeof value, "%g", approx);
            snprintf(precision, sizeof precision, "%u", v->precision);
            if (v->precision != 0) {
                fflush(stdout);
                fprintf(stderr, PROGRAM ": value/precision too large to be printed: '%s/%s' (consider using --to)\n", value, precision);
            } else {
                fflush(stdout);
                fprintf(stderr, PROGRAM ": value too large to be printed: '%s' (consider using --to)\n", value);
            }
            if (convExitCode != 0)
                exit(convExitCode);
        }
        return 0;
    }
    if (x > 26 || approx >= 1e27) {
        if (invalidStyle != INVALID_IGNORE) {
            fflush(stdout);
            fprintf(stderr, PROGRAM ": value too large to be printed: '%g' (cannot handle values > 999Y)\n", approx);
            if (convExitCode != 0)
                exit(convExitCode);
        }
        return 0;
    }
    big_set(&one, 1);
    digits = (int)(userPrecision >= 0 ? userPrecision : v->precision);
    if (scaleTo == SCALE_NONE) {
        round_to_digits(&v->num, &den, v->neg, digits, roundStyle, 0, &q);
        format_decimal(&q, digits, v->neg, out, size);
        return 1;
    }
    {
        unsigned int base = scaleTo == SCALE_SI ? 1000 : 1024;
        int power = 0, d, tenths = 0, bump;
        Big scaled, limit;
        char number[LIMBS * 10 + 80];

        // the power with base^power <= |v| < base^(power+1)
        for (;;) {
            limit = den;
            {
                int i;

                for (i = 0; i <= power; i++)
                    big_mul_small(&limit, base);
            }
            if (power < 8 && big_cmp(&v->num, &limit) >= 0)
                power++;
            else
                break;
        }
        for (;;) {
            scaled = den;
            {
                int i;

                for (i = 0; i < power; i++)
                    big_mul_small(&scaled, base);
            }
            bump = 0;
            tenths = 0;
            if (userPrecision >= 0) {
                int rd;

                d = (int)userPrecision;
                rd = power == 0 ? 0 : d;                    /* below the first suffix the number is rounded to an integer */
                round_to_digits(&v->num, &scaled, v->neg, rd, roundStyle, 0, &q);
                {
                    int k;

                    for (k = rd; k < d; k++)
                        big_mul_small(&q, 10);
                }
            } else {
                Big ten;
                Big tens = scaled;

                big_set(&ten, 10);
                big_mul_small(&tens, 10);
                if (power == 0 && big_cmp(&v->num, &tens) < 0) {
                    d = 0;
                    round_to_digits(&v->num, &scaled, v->neg, 0, roundStyle, 1, &q);
                } else if (power > 0 && big_cmp(&v->num, &tens) < 0) {
                    d = 1;
                    tenths = 1;
                    round_to_digits(&v->num, &scaled, v->neg, 1, roundStyle, 0, &q);
                } else {
                    d = 0;
                    round_to_digits(&v->num, &scaled, v->neg, 0, roundStyle, 0, &q);
                }
            }
            // rounded up to the base: the next suffix
            {
                Big baseBig, shifted = q;
                int i;

                big_set(&baseBig, base);
                for (i = 0; i < d; i++)
                    big_mul_small(&baseBig, 10);
                (void)shifted;
                if (userPrecision >= 0 && power == 0) {
                    for (i = 0; i < 0; i++)
                        ;
                }
                if (power < 8 && big_cmp(&q, &baseBig) >= 0)
                    bump = 1;
            }
            if (!bump)
                break;
            power++;
        }
        if (tenths) {
            Big hundred;

            big_set(&hundred, 100);
            if (big_cmp(&q, &hundred) >= 0) {       // 10.0 is written 10
                big_divmod_small(&q, 10);
                d = 0;
            }
        }
        format_decimal(&q, d, v->neg, number, sizeof number);
        if (power > 0) {
            size_t len = strlen(number);

            number[len++] = suffixLetters[power - 1];
            if (scaleTo == SCALE_IEC_I)
                number[len++] = 'i';
            number[len] = '\0';
        }
        snprintf(out, size, "%s", number);
    }
    return 1;
}

/* ---- fields and lines ---- */

static int field_selected(unsigned long field) {
    size_t i;

    for (i = 0; i < fieldRangeCount; i++) {
        if (field >= fieldRanges[i].first && field <= fieldRanges[i].last)
            return 1;
    }
    return 0;
}

static int is_blank_sep(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

/** Converts one field; writes the result, or the field as it was after a failure. Returns 1 when it was converted. */
static int process_field(const char* original, unsigned long fieldNumber) {
    char* text = _strdup(original);
    char* p;
    char buffer[LIMBS * 10 + 200];
    char padded[LIMBS * 10 + 400];
    Value v;
    const char* end;
    Sse e;
    int ok = 1;
    int trimmed = 0;
    long width = paddingWidth;

    if (text == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        exit(1);
    }
    if (suffix != NULL && strlen(text) > strlen(suffix)) {
        char* possible = text + strlen(text) - strlen(suffix);

        if (strcmp(possible, suffix) == 0) {
            *possible = '\0';
            trimmed = 1;
        }
    }
    (void)trimmed;
    p = text;
    while (*p != '\0' && (*p == ' ' || *p == '\t'))
        p++;
    if (autoPadding)
        width = (p != text || fieldNumber > 1) ? (long)strlen(text) : 0;
    e = parse_human(p, &end, &v, scaleFrom);
    if (e == SSE_OK && *end != '\0') {
        conv_error("invalid suffix in input '%s': %s", p, quote(end));
        ok = 0;
    } else if (e != SSE_OK) {
        switch (e) {
        case SSE_INVALID_NUMBER:
            conv_error("invalid number: %s", quote(p), NULL);
            break;
        case SSE_INVALID_SUFFIX:
            conv_error("invalid suffix in input: %s", quote(p), NULL);
            break;
        case SSE_MISSING_I:
            conv_error("missing 'i' suffix in input: %s (e.g Ki/Mi/Gi)", quote(p), NULL);
            break;
        case SSE_FORBIDDEN_SUFFIX:
            conv_error("rejecting suffix in input: %s (consider using --from)", quote(p), NULL);
            break;
        case SSE_OVERFLOW:
            conv_error("value too large to be converted: %s", quote(p), NULL);
            break;
        default:
            break;
        }
        ok = 0;
    }
    if (ok)
        ok = make_number(&v, buffer, sizeof buffer);
    if (ok) {
        size_t length;

        if (suffix != NULL) {
            size_t n = strlen(buffer);

            snprintf(buffer + n, sizeof buffer - n, "%s", suffix);
        }
        length = strlen(buffer);
        if (width != 0) {
            long w = width < 0 ? -width : width;

            if ((long)length < w) {
                size_t fill = (size_t)w - length;

                if (width > 0) {
                    memset(padded, ' ', fill);
                    memcpy(padded + fill, buffer, length + 1);
                } else {
                    memcpy(padded, buffer, length);
                    memset(padded + length, ' ', fill);
                    padded[length + fill] = '\0';
                }
                snprintf(buffer, sizeof buffer, "%s", padded);
            }
        }
        if (formatPrefix != NULL)
            fputs(formatPrefix, stdout);
        fputs(buffer, stdout);
        if (formatSuffix != NULL)
            fputs(formatSuffix, stdout);
    } else {
        fputs(original, stdout);
        validNumbers = 0;
    }
    free(text);
    return ok;
}

static void process_line(const char* line, int newline) {
    const char* next = line;
    unsigned long field = 0;

    for (;;) {
        const char* start = next;
        const char* endField = start;
        char* piece;
        size_t length;

        ++field;
        if (delimiter >= 0) {
            if (*start != delimiter) {
                while (*endField != '\0' && *endField != delimiter)
                    endField++;
            }
        } else {
            while (*endField != '\0' && is_blank_sep((unsigned char)*endField))
                endField++;
            while (*endField != '\0' && !is_blank_sep((unsigned char)*endField))
                endField++;
        }
        length = (size_t)(endField - start);
        piece = malloc(length + 1);
        memcpy(piece, start, length);
        piece[length] = '\0';
        if (field_selected(field))
            process_field(piece, field);
        else
            fputs(piece, stdout);
        free(piece);
        next = endField;
        if (*next == '\0')
            break;
        fputc(delimiter >= 0 ? delimiter : ' ', stdout);       // one separator, whatever blank it was
        next++;
    }
    if (newline)
        fputc(lineDelimiter, stdout);
}

/* ---- the options ---- */

static void parse_fields(const char* list) {
    const char* p = list;
    size_t capacity = 0;

    for (;;) {
        unsigned long first = 0, last = 0;
        int haveFirst = 0, haveLast = 0, dash = 0;
        Range range;
        const char* item = p;

        if (isdigit((unsigned char)*p)) {
            haveFirst = 1;
            while (isdigit((unsigned char)*p)) {
                first = first * 10 + (unsigned long)(*p - '0');
                p++;
            }
        }
        if (*p == '-') {
            dash = 1;
            p++;
            if (isdigit((unsigned char)*p)) {
                haveLast = 1;
                while (isdigit((unsigned char)*p)) {
                    last = last * 10 + (unsigned long)(*p - '0');
                    p++;
                }
            }
        }
        if (!haveFirst && !dash) {
            if (*p == '\0' || *p == ',')
                usage_die("%s", "fields are numbered from 1");
            usage_die("invalid field value %s", quote(item));
        }
        if (*p != '\0' && *p != ',') {
            usage_die("invalid field value %s", quote(p));
        }
        if (haveFirst && first == 0)
            usage_die("%s", "fields are numbered from 1");
        if (!dash) {
            range.first = first;
            range.last = first;
        } else if (!haveFirst && !haveLast) {
            range.first = 1;
            range.last = ULONG_MAX;
        } else if (!haveFirst) {
            if (last == 0)
                usage_die("%s", "invalid decreasing range");
            range.first = 1;
            range.last = last;
        } else if (!haveLast) {
            range.first = first;
            range.last = ULONG_MAX;
        } else {
            if (last < first)
                usage_die("%s", "invalid decreasing range");
            range.first = first;
            range.last = last;
        }
        if (fieldRangeCount == capacity) {
            capacity = capacity == 0 ? 8 : capacity * 2;
            fieldRanges = realloc(fieldRanges, capacity * sizeof *fieldRanges);
        }
        fieldRanges[fieldRangeCount++] = range;
        if (*p == '\0')
            break;
        p++;
    }
}

static u64 unit_size(const char* text) {
    char withOne[128];
    const char* p = text;
    Value v;
    Sse e;
    const char* end;

    if (valid_suffix((unsigned char)text[0]) && strlen(text) < 100) {
        snprintf(withOne, sizeof withOne, "1%s", text);
        p = withOne;
    }
    e = parse_human(p, &end, &v, SCALE_AUTO);
    if (e != SSE_OK || *end != '\0' || v.precision != 0 || v.neg || v.num.n == 0)
        die("invalid unit size: %s", quote(text));
    {
        if (v.num.n > 2)
            die("invalid unit size: %s", quote(text));
        return v.num.n == 0 ? 0 : (u64)v.num.d[0] | (v.num.n > 1 ? (u64)v.num.d[1] << 32 : 0);
    }
}

static void parse_format(const char* fmt) {
    size_t i = 0, prefixLength = 0, suffixPos;
    long pad = 0;
    int zero = 0, minus = 0;

    formatStr = fmt;
    for (i = 0; !(fmt[i] == '%' && fmt[i + 1] != '%'); i += (fmt[i] == '%') ? 2 : 1) {
        if (fmt[i] == '\0')
            die("format %s has no %% directive", quote(fmt));
        prefixLength++;
    }
    // the prefix is written as it is
    if (fmt[i + 1] == '\0')
        die("format %s ends in %%", quote(fmt));
    i++;
    while (fmt[i] == ' ')
        i++;
    for (;; i++) {
        if (fmt[i] == '\'')
            grouping = 1;
        else if (fmt[i] == '0')
            zero = 1;
        else if (fmt[i] == '-')
            minus = 1;
        else
            break;
    }
    {
        char* endptr;

        pad = strtol(fmt + i, &endptr, 10);
        i = (size_t)(endptr - fmt);
    }
    if (fmt[i] == '.') {
        char* endptr;

        i++;
        userPrecision = strtol(fmt + i, &endptr, 10);
        if (userPrecision < 0)
            userPrecision = 0;
        i = (size_t)(endptr - fmt);
    }
    if (fmt[i] == '\0')
        die("format %s ends in %%", quote(fmt));
    if (fmt[i] != 'f') {
        char message[2200];

        snprintf(message, sizeof message, "invalid format %s, directive must be %%[0]['][-][N][.][N]f", quote(fmt));
        die("%s", message);
    }
    i++;
    suffixPos = i;
    for (; fmt[i] != '\0'; i += (fmt[i] == '%') ? 2 : 1) {
        if (fmt[i] == '%' && fmt[i + 1] != '%')
            die("format %s has too many %% directives", quote(fmt));
        if (fmt[i] == '%' && fmt[i + 1] == '\0')
            break;
    }
    if (pad != 0) {
        if (minus)
            pad = -pad;
        if (debug && paddingWidth != 0)
            fprintf(stderr, PROGRAM ": --format padding overriding --padding\n");
        paddingWidth = pad;
    } else if (minus && 0) {
        paddingWidth = 0;
    }
    if (zero && !minus && pad > 0)
        zeroPaddingWidth = pad;
    if (prefixLength > 0) {
        formatPrefix = malloc(prefixLength + 1);
        memcpy(formatPrefix, fmt, prefixLength);
        formatPrefix[prefixLength] = '\0';
    }
    if (fmt[suffixPos] != '\0')
        formatSuffix = _strdup(fmt + suffixPos);
}

static const char* const scaleFromNames[] = { "none", "auto", "si", "iec", "iec-i" };
static const char* const scaleToNames[] = { "none", "si", "iec", "iec-i" };
static const char* const roundNames[] = { "up", "down", "from-zero", "towards-zero", "nearest" };
static const char* const invalidNames[] = { "abort", "fail", "warn", "ignore" };

static int on_option(int id, const char* value, void* context) {
    (void)context;
    switch (id) {
    case OPT_DEBUG:
        debug = 1;
        break;
    case OPT_DELIMITER:
        if (value[1] != '\0' && value[0] != '\0')
            die("%s", "the delimiter must be a single character");
        delimiter = value[0] == '\0' ? 256 : (unsigned char)value[0];
        break;
    case OPT_FIELD:
        parse_fields(value);
        break;
    case OPT_FORMAT:
        pendingFormat = value;
        break;
    case OPT_FROM:
        scaleFrom = (Scale)argmatch("from", value, scaleFromNames, 5);
        break;
    case OPT_TO: {
        int k = argmatch("to", value, scaleToNames, 4);

        scaleTo = k == 0 ? SCALE_NONE : (Scale)(k + 1);
        break;
    }
    case OPT_FROM_UNIT:
        fromUnit = unit_size(value);
        break;
    case OPT_TO_UNIT:
        toUnit = unit_size(value);
        break;
    case OPT_GROUPING:
        grouping = 1;
        groupingOption = 1;
        break;
    case OPT_HEADER:
        headerGiven = 1;
        if (value == NULL) {
            header = 1;
        } else {
            char* end;
            unsigned long n = strtoul(value, &end, 10);

            if (*value == '\0' || *end != '\0' || n == 0 || value[0] == '-')
                die("invalid header value %s", quote(value));
            header = n;
        }
        break;
    case OPT_INVALID:
        invalidStyle = (Invalid)argmatch("invalid", value, invalidNames, 4);
        convExitCode = invalidStyle == INVALID_ABORT ? 2 : 0;
        break;
    case OPT_PADDING: {
        char* end;
        long n = strtol(value, &end, 10);

        if (*value == '\0' || *end != '\0' || n == 0)
            die("invalid padding value %s", quote(value));
        paddingWidth = n;
        break;
    }
    case OPT_ROUND:
        roundStyle = (Round)argmatch("round", value, roundNames, 5);
        break;
    case OPT_SUFFIX:
        suffix = value;
        break;
    case OPT_ZERO:
        lineDelimiter = 0;
        break;
    default:
        break;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, i;

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (fieldRangeCount == 0) {
        fieldRanges = malloc(sizeof *fieldRanges);
        fieldRanges[0].first = 1;
        fieldRanges[0].last = 1;
        fieldRangeCount = 1;
    }
    if (groupingOption && pendingFormat != NULL)
        die("%s", "--grouping cannot be combined with --format");
    if (pendingFormat != NULL)
        parse_format(pendingFormat);
    if (groupingOption && scaleTo != SCALE_NONE)
        die("%s", "grouping cannot be combined with --to");
    autoPadding = paddingWidth == 0 && delimiter < 0;
    if (debug && scaleFrom == SCALE_NONE && scaleTo == SCALE_NONE && !grouping && paddingWidth == 0 && formatStr == NULL)
        fprintf(stderr, PROGRAM ": no conversion option specified\n");
    if (count > 0) {
        if (debug && headerGiven)
            fprintf(stderr, PROGRAM ": --header ignored with command-line input\n");
        for (i = 0; i < count; i++)
            process_line(operands[i], 1);
    } else {
        size_t length, offset = 0;
        unsigned char* text;
        unsigned long headerLeft = header;

        text = text_slurp(stdin, &length);
        if (text == NULL)
            text = (unsigned char*)calloc(1, 1);
        while (offset < length) {
            size_t start = offset, end;
            int terminated = 0;
            char* line;

            while (offset < length && text[offset] != (unsigned char)lineDelimiter)
                offset++;
            end = offset;
            if (offset < length) {
                terminated = 1;
                offset++;
            }
            if (lineDelimiter == '\n' && terminated && end > start && text[end - 1] == '\r')
                end--;
            line = malloc(end - start + 1);
            memcpy(line, text + start, end - start);
            line[end - start] = '\0';
            if (headerLeft > 0) {
                headerLeft--;
                fputs(line, stdout);
                if (terminated)
                    fputc(lineDelimiter, stdout);
            } else {
                process_line(line, terminated);
            }
            free(line);
        }
        free(text);
    }
    if (!validNumbers) {
        if (debug && invalidStyle != INVALID_ABORT)
            fprintf(stderr, PROGRAM ": failed to convert some of the input numbers\n");
        return invalidStyle == INVALID_FAIL ? 2 : 0;
    }
    return 0;
}
