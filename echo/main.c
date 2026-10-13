#include <stdio.h>
#include <string.h>
#include "args.h"

#define VERSION "0.1"

static void print_usage(void) {
    puts("Usage: echo [SHORT-OPTION]... [STRING]...\n"
         "  or:  echo LONG-OPTION\n"
         "Echo the STRING(s) to standard output.\n"
         "\n"
         "  -n             do not output the trailing newline\n"
         "  -e             enable interpretation of backslash escapes\n"
         "  -E             disable interpretation of backslash escapes (default)\n"
         "  --help         display this help and exit\n"
         "  --version      output version information and exit\n"
         "\n"
         "If -e is in effect, the following sequences are recognized:\n"
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
         "  \\0NNN   byte with octal value NNN (1 to 3 digits)\n"
         "  \\xHH    byte with hexadecimal value HH (1 to 2 digits)");
}

static int hex_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * Prints s while interpreting backslash escapes.
 * Returns 1 when "\c" was found, which stops all further output.
 */
static int print_escaped(const char* s) {
    for (; *s != '\0'; s++) {
        if (*s != '\\' || s[1] == '\0') {
            putchar(*s);
            continue;
        }

        s++;
        switch (*s) {
            case '\\': putchar('\\'); break;
            case 'a':  putchar('\a'); break;
            case 'b':  putchar('\b'); break;
            case 'c':  return 1;
            case 'e':  putchar(27);   break;
            case 'f':  putchar('\f'); break;
            case 'n':  putchar('\n'); break;
            case 'r':  putchar('\r'); break;
            case 't':  putchar('\t'); break;
            case 'v':  putchar('\v'); break;
            case '0': {
                int value = 0, digits = 0;
                while (digits < 3 && s[1] >= '0' && s[1] <= '7') {
                    value = value * 8 + (*++s - '0');
                    digits++;
                }
                putchar(value);
                break;
            }
            case 'x': {
                int value = 0, digits = 0;
                if (hex_value(s[1]) < 0) {
                    // not a valid escape; print as is
                    putchar('\\');
                    putchar('x');
                    break;
                }
                while (digits < 2 && hex_value(s[1]) >= 0) {
                    value = value * 16 + hex_value(*++s);
                    digits++;
                }
                putchar(value);
                break;
            }
            default:
                putchar('\\');
                putchar(*s);
                break;
        }
    }
    return 0;
}

/**
 * Checks whether arg is a valid option group such as "-n", "-e" or "-neE",
 * and applies it. Anything else (including a lone "-") is plain text.
 */
static int parse_option(const char* arg, int* newline, int* escapes) {
    const char* p;
    int n = *newline, e = *escapes;

    if (arg[0] != '-' || arg[1] == '\0')
        return 0;

    for (p = arg + 1; *p != '\0'; p++) {
        switch (*p) {
            case 'n': n = 0; break;
            case 'e': e = 1; break;
            case 'E': e = 0; break;
            default:  return 0;
        }
    }

    *newline = n;
    *escapes = e;
    return 1;
}

int main(int argc, char* argv[]) {
    int newline = 1, escapes = 0;
    int i = 1;

    args_real_arguments(&argc, &argv);

    if (argc == 2) {
        if (strcmp(argv[1], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[1], "--version") == 0) {
            puts("echo (Winshell) " VERSION);
            return 0;
        }
    }

    while (i < argc && parse_option(argv[i], &newline, &escapes))
        i++;

    for (; i < argc; i++) {
        if (escapes) {
            if (print_escaped(argv[i]))
                return 0;   // "\c": no further output, not even the newline
        } else {
            fputs(argv[i], stdout);
        }
        if (i + 1 < argc)
            putchar(' ');
    }

    if (newline)
        putchar('\n');
    return 0;
}
