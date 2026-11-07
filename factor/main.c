/*
 * factor: prints the prime factors of numbers, as GNU factor does (one line per number: "N: p1 p2 ..."). The
 * numbers are read from the operands, or from standard input (separated by blanks and line feeds), and may go up to
 * 2^64 - 1. Trial division takes the small primes, then Miller-Rabin and Pollard's rho take the rest.
 */

#include <ctype.h>
#include <fcntl.h>
#include <intrin.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "factor"

static void print_usage(void) {
    puts("Usage: factor [NUMBER]...\n"
         "  or:  factor OPTION\n"
         "Print the prime factors of each specified integer NUMBER.  If none\n"
         "are specified on the command line, read them from standard input.\n"
         "\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit");
}

static const ArgsOption options[] = { { 0 } };
static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    (void)id;
    (void)value;
    (void)context;
    return 0;
}

typedef unsigned __int64 u64;

static u64 mulmod(u64 a, u64 b, u64 n) {
    u64 high, low, rest;

    low = _umul128(a, b, &high);
    _udiv128(high, low, n, &rest);          // high < n because a and b are below n
    return rest;
}

static u64 powmod(u64 base, u64 exponent, u64 n) {
    u64 result = 1;

    base %= n;
    while (exponent > 0) {
        if (exponent & 1)
            result = mulmod(result, base, n);
        base = mulmod(base, base, n);
        exponent >>= 1;
    }
    return result;
}

static int is_prime(u64 n) {
    static const u64 witnesses[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };
    u64 d = n - 1;
    int r = 0, i;

    if (n < 2)
        return 0;
    for (i = 0; i < 12; i++) {
        if (n == witnesses[i])
            return 1;
        if (n % witnesses[i] == 0)
            return 0;
    }
    while ((d & 1) == 0) {
        d >>= 1;
        r++;
    }
    for (i = 0; i < 12; i++) {
        u64 x = powmod(witnesses[i], d, n);
        int k, composite = 1;

        if (x == 1 || x == n - 1)
            continue;
        for (k = 1; k < r; k++) {
            x = mulmod(x, x, n);
            if (x == n - 1) {
                composite = 0;
                break;
            }
        }
        if (composite)
            return 0;
    }
    return 1;
}

static u64 gcd(u64 a, u64 b) {
    while (b != 0) {
        u64 t = a % b;

        a = b;
        b = t;
    }
    return a;
}

/** A factor of the composite odd n (not necessarily prime), by Pollard's rho with Brent's cycle finding. */
static u64 rho(u64 n) {
    u64 c;

    for (c = 1;; c++) {
        u64 x = 2, y = 2, d = 1;

        while (d == 1) {
            x = (mulmod(x, x, n) + c) % n;
            y = (mulmod(y, y, n) + c) % n;
            y = (mulmod(y, y, n) + c) % n;
            d = gcd(x > y ? x - y : y - x, n);
        }
        if (d != n)
            return d;
    }
}

static int compare_u64(const void* a, const void* b) {
    u64 x = *(const u64*)a, y = *(const u64*)b;

    return x < y ? -1 : x > y;
}

static void split(u64 n, u64* factors, int* count) {
    u64 d;

    if (n == 1)
        return;
    if (is_prime(n)) {
        factors[(*count)++] = n;
        return;
    }
    d = rho(n);
    split(d, factors, count);
    split(n / d, factors, count);
}

static void print_factors(const char* text, u64 n) {
    u64 factors[70];
    int count = 0, i;
    static const unsigned small[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97 };
    u64 rest = n;

    printf("%s:", text);
    for (i = 0; i < (int)(sizeof small / sizeof small[0]) && rest > 1; i++)
        while (rest % small[i] == 0) {
            factors[count++] = small[i];
            rest /= small[i];
        }
    if (rest > 1) {
        int before = count;

        split(rest, factors, &count);
        qsort(factors + before, (size_t)(count - before), sizeof factors[0], compare_u64);
    }
    for (i = 0; i < count; i++)
        printf(" %llu", (unsigned long long)factors[i]);
    putchar('\n');
}

/** Reads one number token. Returns 0 and the value, or -1 when it is not a number (negative, text, too large). */
static int parse_number(const char* token, u64* value, char* text) {
    const char* p = token;
    u64 n = 0;
    size_t length = 0;

    while (isspace((unsigned char)*p))
        p++;
    if (*p == '+')
        p++;
    if (!isdigit((unsigned char)*p))
        return -1;
    while (isdigit((unsigned char)*p)) {
        unsigned digit = (unsigned)(*p - '0');

        if (n > (0xffffffffffffffffULL - digit) / 10)
            return -2;
        n = n * 10 + digit;
        text[length++] = *p;
        p++;
    }
    text[length] = '\0';
    while (isspace((unsigned char)*p))
        p++;
    if (*p != '\0')
        return -1;
    // the number is written without leading zeros and without the plus sign
    {
        char* z = text;

        while (z[0] == '0' && z[1] != '\0')
            z++;
        memmove(text, z, strlen(z) + 1);
    }
    *value = n;
    return 0;
}

static int handle(const char* token) {
    u64 n;
    char text[40];
    int r = parse_number(token, &n, text);

    if (r == -2) {
        fprintf(stderr, PROGRAM ": '%s' is too large\n", token);
        return 1;
    }
    if (r != 0) {
        fprintf(stderr, PROGRAM ": '%s' is not a valid positive integer\n", token);
        return 1;
    }
    print_factors(text, n);
    return 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, i, status = 0;

    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 0) {
        for (i = 0; i < count; i++)
            status |= handle(operands[i]);
    } else {
        char token[256];
        int c, length = 0;

        for (;;) {
            c = getchar();
            if (c == EOF || isspace(c)) {
                if (length > 0) {
                    token[length] = '\0';
                    status |= handle(token);
                    length = 0;
                    fflush(stdout);
                }
                if (c == EOF)
                    break;
            } else if (length < (int)sizeof token - 1) {
                token[length++] = (char)c;
            }
        }
    }
    return fflush(stdout) != 0 ? 1 : status;
}
