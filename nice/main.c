/*
 * nice: runs a command with a changed niceness, as GNU nice does, or prints the niceness of the process. Windows has
 * priority classes instead of the niceness -20..19, so the number is mapped: normal is 0, below normal 10, idle 19,
 * above normal -5, high -10 and realtime -20; a niceness picks the nearest class for the command. The legacy forms -N,
 * --N and -+N are adjustments as well.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "exec.h"

#define VERSION "0.1"
#define PROGRAM "nice"

static void print_usage(void) {
    puts("Usage: nice [OPTION] [COMMAND [ARG]...]\n"
         "Run COMMAND with an adjusted niceness, which affects process scheduling.\n"
         "With no COMMAND, print the current niceness.  Niceness values range from\n"
         "-20 (most favorable to the process) to 19 (least favorable to the process).\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  -n, --adjustment=N   add integer N to the niceness (default 10)\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "NOTE: your shell may have its own version of nice, which usually supersedes\n"
         "the version described here.  Please refer to your shell's documentation\n"
         "for details about the options it supports.");
}

static void usage_error(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

static int current_niceness(void) {
    switch (GetPriorityClass(GetCurrentProcess())) {
    case IDLE_PRIORITY_CLASS: return 19;
    case BELOW_NORMAL_PRIORITY_CLASS: return 10;
    case ABOVE_NORMAL_PRIORITY_CLASS: return -5;
    case HIGH_PRIORITY_CLASS: return -10;
    case REALTIME_PRIORITY_CLASS: return -20;
    default: return 0;
    }
}

static DWORD class_for(int niceness) {
    if (niceness >= 15)
        return IDLE_PRIORITY_CLASS;
    if (niceness >= 5)
        return BELOW_NORMAL_PRIORITY_CLASS;
    if (niceness <= -20)
        return REALTIME_PRIORITY_CLASS;
    if (niceness <= -10)
        return HIGH_PRIORITY_CLASS;
    if (niceness <= -3)
        return ABOVE_NORMAL_PRIORITY_CLASS;
    return NORMAL_PRIORITY_CLASS;
}

/** The adjustment as xstrtol reads it: blanks, a sign, digits; a huge one is clamped. Returns 0, or -1 when invalid. */
static int parse_adjustment(const char* text, long* out) {
    const char* p = text;
    int negative = 0;
    long long value = 0;
    int any = 0;

    while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
        p++;
    if (*p == '-' || *p == '+') {
        negative = *p == '-';
        p++;
    }
    for (; isdigit((unsigned char)*p); p++) {
        any = 1;
        if (value < 1000000)
            value = value * 10 + (*p - '0');
    }
    if (!any || *p != '\0')
        return -1;
    *out = (long)(negative ? -value : value);
    return 0;
}

int main(int argc, char* argv[]) {
    const char* adjustmentText = NULL;
    int i = 1;
    int niceness;
    long adjustment = 10;

    _setmode(_fileno(stdout), _O_BINARY);
    while (i < argc) {
        const char* s = argv[i];

        if (s[0] == '-' && (s[1] == '-' ? isdigit((unsigned char)s[2]) : (s[1] == '+' || isdigit((unsigned char)s[1])))) {
            adjustmentText = s + 1;                 // -N, -+N and --N are adjustments
            i++;
            continue;
        }
        if (strcmp(s, "--") == 0) {
            i++;
            break;
        }
        if (strcmp(s, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(s, "--version") == 0) {
            printf(PROGRAM " (Winshell) " VERSION "\n");
            return 0;
        }
        if (strncmp(s, "--adjustment", 12) == 0 && (s[12] == '\0' || s[12] == '=')) {
            if (s[12] == '=') {
                adjustmentText = s + 13;
            } else if (i + 1 < argc) {
                adjustmentText = argv[++i];
            } else {
                fprintf(stderr, PROGRAM ": option '--adjustment' requires an argument\n");
                usage_error();
                return 125;
            }
            i++;
            continue;
        }
        if (s[0] == '-' && s[1] == '-' && s[2] != '\0') {
            fprintf(stderr, PROGRAM ": unrecognized option '%s'\n", s);
            usage_error();
            return 125;
        }
        if (s[0] == '-' && s[1] == 'n') {
            if (s[2] != '\0') {
                adjustmentText = s + 2;
            } else if (i + 1 < argc) {
                adjustmentText = argv[++i];
            } else {
                fprintf(stderr, PROGRAM ": option requires an argument -- 'n'\n");
                usage_error();
                return 125;
            }
            i++;
            continue;
        }
        if (s[0] == '-' && s[1] != '\0') {
            fprintf(stderr, PROGRAM ": invalid option -- '%c'\n", s[1]);
            usage_error();
            return 125;
        }
        break;
    }
    if (adjustmentText != NULL && parse_adjustment(adjustmentText, &adjustment) != 0) {
        fprintf(stderr, PROGRAM ": invalid adjustment '%s'\n", adjustmentText);
        return 125;
    }
    if (i >= argc) {
        if (adjustmentText != NULL) {
            fprintf(stderr, PROGRAM ": a command must be given with an adjustment\n");
            usage_error();
            return 125;
        }
        printf("%d\n", current_niceness());
        return 0;
    }
    niceness = current_niceness();
    if (adjustmentText == NULL)
        adjustment = 10;
    {
        long wanted = niceness + adjustment;

        if (wanted > 19)
            wanted = 19;
        if (wanted < -20)
            wanted = -20;
        niceness = (int)wanted;
    }
    return exec_run(PROGRAM, 0, argv + i, class_for(niceness), GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE),
                    GetStdHandle(STD_ERROR_HANDLE));
}
