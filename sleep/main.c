#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "sleep"

static void print_usage(void) {
    puts("Usage: sleep NUMBER[SUFFIX]...\n"
         "  or:  sleep OPTION\n"
         "Pause for NUMBER seconds. SUFFIX may be 's' for seconds (the default),\n"
         "'m' for minutes, 'h' for hours or 'd' for days. NUMBER need not be an\n"
         "integer. Given two or more arguments, pause for the amount of time\n"
         "specified by the sum of their values.\n"
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

/** Seconds of one operand: a number and an optional suffix s, m, h or d. Returns 0, or -1 when it is not one. */
static int parse_interval(const char* text, double* seconds) {
    char* end;
    double value;
    double unit = 1.0;

    if (*text == '\0' || *text == '-' || *text == '+' || isspace((unsigned char)*text))
        return -1;
    value = strtod(text, &end);
    if (end == text || value != value || value < 0)
        return -1;
    if (*end != '\0') {
        switch (*end) {
            case 's': unit = 1.0; break;
            case 'm': unit = 60.0; break;
            case 'h': unit = 3600.0; break;
            case 'd': unit = 86400.0; break;
            default:  return -1;
        }
        if (end[1] != '\0')
            return -1;
    }
    *seconds = value * unit;
    return 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, i, failed = 0;
    double total = 0.0;

    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing operand\nTry '" PROGRAM " --help' for more information.\n");
        return 1;
    }
    for (i = 0; i < count; i++) {
        double seconds;
        if (parse_interval(operands[i], &seconds) != 0) {
            fprintf(stderr, PROGRAM ": invalid time interval '%s'\n", operands[i]);
            failed = 1;
            continue;
        }
        total += seconds;
    }
    if (failed) {
        fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
        return 1;
    }

    // Sleep takes milliseconds; a very long (or infinite) time goes on in pieces
    while (total > 0.0) {
        double piece = total > 86400.0 ? 86400.0 : total;
        Sleep((DWORD)(piece * 1000.0 + 0.5));
        if (isinf(total))
            continue;
        total -= piece;
    }
    return 0;
}
