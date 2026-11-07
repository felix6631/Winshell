/*
 * seq: prints a sequence of numbers, as GNU seq does.
 *
 * Whole numbers are counted exactly; as soon as one of the numbers has a fraction (or an exponent) the sequence
 * is computed in floating point and written with as many decimals as the most precise of FIRST and INCREMENT.
 * Negative numbers are operands (seq -3 3), not options.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION "0.1"
#define PROGRAM "seq"

static void print_usage(void) {
    puts("Usage: seq [OPTION]... LAST\n"
         "  or:  seq [OPTION]... FIRST LAST\n"
         "  or:  seq [OPTION]... FIRST INCREMENT LAST\n"
         "Print numbers from FIRST to LAST, in steps of INCREMENT.\n"
         "\n"
         "  -f, --format=FORMAT      use printf style floating-point FORMAT\n"
         "  -s, --separator=STRING   use STRING to separate numbers (default: \\n)\n"
         "  -w, --equal-width        equalize width by padding with leading zeroes\n"
         "      --help               display this help and exit\n"
         "      --version            output version information and exit\n"
         "\n"
         "If FIRST or INCREMENT is omitted, it defaults to 1.  That is, an omitted INCREMENT defaults to\n"
         "1 even when LAST is smaller than FIRST.\n"
         "The sequence of numbers ends when the sum of the current number and INCREMENT would\n"
         "become greater than LAST.\n"
         "FIRST, INCREMENT, and LAST are interpreted as floating point values.\n"
         "INCREMENT is usually positive if FIRST is smaller than LAST, and INCREMENT is\n"
         "usually negative if FIRST is greater than LAST.\n"
         "INCREMENT must not be 0; none of FIRST, INCREMENT and LAST may be NaN.\n"
         "FORMAT must be suitable for printing one argument of type 'double';\n"
         "it defaults to %.PRECf if FIRST, INCREMENT, and LAST are all fixed point\n"
         "decimal numbers with maximum precision PREC, and to %g otherwise.");
}

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

typedef struct {
    int    isInteger;       // a whole number that fits in a long long
    long long whole;
    double value;
    int    decimals;        // digits after the point as written
} Number;

/** Reads FIRST, INCREMENT or LAST. Returns 0, or -1 after a message. */
static int parse_number(const char* text, Number* n) {
    char* end;
    const char* p = text;
    const char* point;

    while (isspace((unsigned char)*p))
        p++;
    if (*p == '\0') {
        fprintf(stderr, PROGRAM ": invalid floating point argument: '%s'\n", text);
        return -1;
    }
    errno = 0;
    n->value = strtod(p, &end);
    if (end == p || *end != '\0' || isnan(n->value)) {
        fprintf(stderr, PROGRAM ": invalid %s argument: '%s'\n",
                end != p && isnan(n->value) ? "'not-a-number'" : "floating point", text);
        return -1;
    }
    n->isInteger = 0;
    n->whole = 0;
    n->decimals = 0;
    if (strpbrk(p, "xXeEpPnN") == NULL && !isinf(n->value)) {
        point = strchr(p, '.');
        if (point != NULL) {
            const char* q = point + 1;

            while (isdigit((unsigned char)*q)) {
                n->decimals++;
                q++;
            }
        } else {
            char* integerEnd;

            errno = 0;
            n->whole = strtoll(p, &integerEnd, 10);
            n->isInteger = *integerEnd == '\0' && errno != ERANGE;
        }
    } else if (strpbrk(p, "xX") != NULL) {
        // 0x10 and the like: whole when it has no fraction
        if (n->value == floor(n->value) && fabs(n->value) < 9.0e18) {
            n->whole = (long long)n->value;
            n->isInteger = strchr(p, '.') == NULL && strpbrk(p, "pP") == NULL;
        }
    }
    return 0;
}

/**
 * Checks the format of -f: one conversion of a,A,e,E,f,F,g,G (flags, width and precision allowed), other text
 * and %% as they are. Returns 0, or -1 after a message.
 */
static int check_format(const char* format) {
    const char* p = format;
    int directives = 0;

    while (*p != '\0') {
        if (*p != '%') {
            p++;
            continue;
        }
        p++;
        if (*p == '%') {
            p++;
            continue;
        }
        while (*p != '\0' && strchr("-+ #0'", *p) != NULL)
            p++;
        while (isdigit((unsigned char)*p))
            p++;
        if (*p == '.') {
            p++;
            while (isdigit((unsigned char)*p))
                p++;
        }
        if (*p == '\0') {
            fprintf(stderr, PROGRAM ": format '%s' ends in %%\n", format);
            return -1;
        }
        if (strchr("aAeEfFgG", *p) == NULL) {
            fprintf(stderr, PROGRAM ": format '%s' has unknown %%%c directive\n", format, *p);
            return -1;
        }
        if (++directives > 1) {
            fprintf(stderr, PROGRAM ": format '%s' has too many %% directives\n", format);
            return -1;
        }
        p++;
    }
    if (directives == 0) {
        fprintf(stderr, PROGRAM ": format '%s' has no %% directive\n", format);
        return -1;
    }
    return 0;
}

/** Writes the format with its one conversion applied to the value, and %% as %. */
static void print_formatted(const char* format, double value) {
    const char* p = format;

    while (*p != '\0') {
        if (*p == '%' && p[1] == '%') {
            putchar('%');
            p += 2;
        } else if (*p == '%') {
            char spec[64];
            size_t n = 0;

            spec[n++] = *p++;
            while (*p != '\0' && strchr("aAeEfFgG", *p) == NULL && n < sizeof spec - 3)
                spec[n++] = *p++;
            spec[n++] = *p++;
            spec[n] = '\0';
            printf(spec, value);
        } else {
            putchar(*p++);
        }
    }
}

int main(int argc, char* argv[]) {
    const char* format = NULL;
    const char* separator = "\n";
    int equalWidth = 0;
    const char* operands[3];
    int operandCount = 0, i, noMoreOptions = 0;
    Number first, step, last;
    int decimals;
    int useFormat;

    _setmode(_fileno(stdout), _O_BINARY);

    for (i = 1; i < argc; i++) {
        const char* a = argv[i];

        if (noMoreOptions || a[0] != '-' || a[1] == '\0' || isdigit((unsigned char)a[1]) || (a[1] == '.' && isdigit((unsigned char)a[2]))) {
            if (operandCount == 3) {
                fprintf(stderr, PROGRAM ": extra operand '%s'\n", a);
                try_help();
                return 1;
            }
            operands[operandCount++] = a;
            continue;
        }
        if (strcmp(a, "--") == 0) {
            noMoreOptions = 1;
        } else if (strcmp(a, "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(a, "--version") == 0) {
            puts(PROGRAM " (Winshell) " VERSION);
            return 0;
        } else if (strncmp(a, "--format", 8) == 0 && (a[8] == '=' || a[8] == '\0')) {
            if (a[8] == '=') {
                format = a + 9;
            } else if (i + 1 < argc) {
                format = argv[++i];
            } else {
                fprintf(stderr, PROGRAM ": option '--format' requires an argument\n");
                try_help();
                return 1;
            }
        } else if (strncmp(a, "--separator", 11) == 0 && (a[11] == '=' || a[11] == '\0')) {
            if (a[11] == '=') {
                separator = a + 12;
            } else if (i + 1 < argc) {
                separator = argv[++i];
            } else {
                fprintf(stderr, PROGRAM ": option '--separator' requires an argument\n");
                try_help();
                return 1;
            }
        } else if (strcmp(a, "--equal-width") == 0) {
            equalWidth = 1;
        } else if (a[1] != '-') {
            // grouped short options: -w, -s SEP, -f FORMAT (the value may be attached)
            const char* o;

            for (o = a + 1; *o != '\0'; o++) {
                if (*o == 'w') {
                    equalWidth = 1;
                } else if (*o == 's' || *o == 'f') {
                    const char* value;

                    if (o[1] != '\0') {
                        value = o + 1;
                    } else if (i + 1 < argc) {
                        value = argv[++i];
                    } else {
                        fprintf(stderr, PROGRAM ": option requires an argument -- '%c'\n", *o);
                        try_help();
                        return 1;
                    }
                    if (*o == 's')
                        separator = value;
                    else
                        format = value;
                    break;
                } else {
                    fprintf(stderr, PROGRAM ": invalid option -- '%c'\n", *o);
                    try_help();
                    return 1;
                }
            }
        } else {
            fprintf(stderr, PROGRAM ": unrecognized option '%s'\n", a);
            try_help();
            return 1;
        }
    }

    if (operandCount == 0) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        try_help();
        return 1;
    }
    if (format != NULL && equalWidth) {
        fprintf(stderr, PROGRAM ": format string may not be specified when printing equal width strings\n");
        try_help();
        return 1;
    }
    if (format != NULL && check_format(format) != 0) {
        try_help();
        return 1;
    }

    // FIRST and INCREMENT default to 1
    memset(&first, 0, sizeof first);
    first.isInteger = 1;
    first.whole = 1;
    first.value = 1;
    step = first;
    if (operandCount >= 2 && parse_number(operands[0], &first) != 0) {
        try_help();
        return 1;
    }
    if (operandCount == 3 && parse_number(operands[1], &step) != 0) {
        try_help();
        return 1;
    }
    if (parse_number(operands[operandCount - 1], &last) != 0) {
        try_help();
        return 1;
    }
    if (step.value == 0) {
        fprintf(stderr, PROGRAM ": invalid Zero increment value: '%s'\n", operands[1]);
        try_help();
        return 1;
    }

    decimals = first.decimals > step.decimals ? first.decimals : step.decimals;
    useFormat = format != NULL;

    if (!useFormat && first.isInteger && step.isInteger && last.isInteger) {
        // whole numbers, counted exactly
        long long value = first.whole, increment = step.whole, end = last.whole;
        int width = 0, started = 0;

        if (equalWidth) {
            char a[32], b[32];
            int la = snprintf(a, sizeof a, "%lld", first.whole), lb = snprintf(b, sizeof b, "%lld", last.whole);

            width = la > lb ? la : lb;
        }
        for (;;) {
            if (increment > 0 ? value > end : value < end)
                break;
            if (started)
                fputs(separator, stdout);
            started = 1;
            if (width > 0)
                printf("%0*lld", width, value);
            else
                printf("%lld", value);
            if ((increment > 0 && value > INT64_MAX - increment) || (increment < 0 && value < INT64_MIN - increment))
                break;
            value += increment;
        }
        if (started)
            putchar('\n');
    } else {
        // floating point: the n-th number is first + n * step, so that errors do not add up
        double end = last.value;
        unsigned long long n;
        int started = 0, width = 0;
        char formatBuffer[32];

        if (!useFormat) {
            snprintf(formatBuffer, sizeof formatBuffer, "%%.%df", decimals);
            if (equalWidth) {
                char a[512], b[512];
                int la = snprintf(a, sizeof a, formatBuffer, first.value);
                int lb = snprintf(b, sizeof b, formatBuffer, last.value);

                width = la > lb ? la : lb;
            }
        }
        for (n = 0;; n++) {
            double value = first.value + (double)n * step.value;

            if (step.value > 0 ? value > end : value < end)
                break;
            if (started)
                fputs(separator, stdout);
            started = 1;
            if (useFormat) {
                print_formatted(format, value);
            } else if (width > 0) {
                printf("%0*.*f", width, decimals, value);
            } else {
                printf("%.*f", decimals, value);
            }
            if (isinf(value))
                break;
        }
        if (started)
            putchar('\n');
    }
    return fflush(stdout) != 0 ? 1 : 0;
}
