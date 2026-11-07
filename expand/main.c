/*
 * expand: converts tabs to spaces, as GNU expand does. A character is one column, a backspace goes back
 * one; with -i only the tabs and spaces at the start of a line are converted.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "tabstops.h"
#include "textutil.h"
#include "utf8.h"

#define VERSION "0.1"
#define PROGRAM "expand"

static void print_usage(void) {
    puts("Usage: expand [OPTION]... [FILE]...\n"
         "Convert tabs in each FILE to spaces, writing to standard output.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -i, --initial    do not convert tabs after non blanks\n"
         "  -t, --tabs=N     have tabs N characters apart, not 8\n"
         "  -t, --tabs=LIST  use comma separated list of tab positions.\n"
         "                     The last specified position can be prefixed with '/'\n"
         "                     to specify a tab size to use after the last\n"
         "                     explicitly specified tab stop.  Also a prefix of '+'\n"
         "                     can be used to align remaining tab stops relative to\n"
         "                     the last specified tab stop instead of the first column\n"
         "      --help       display this help and exit\n"
         "      --version    output version information and exit");
}

enum { OPT_INITIAL = 1, OPT_TABS };

static const ArgsOption options[] = {
    { OPT_INITIAL, 'i', "initial", ARGS_NONE },
    { OPT_TABS,    't', "tabs",    ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 1 };

typedef struct {
    int      initial;
    TabStops stops;
    int      failed;
} Settings;

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;

    if (id == OPT_INITIAL)
        return s->initial = 1, 0;
    // -t LIST and the obsolete -N (the digits are the tab size)
    if (tabstops_add(PROGRAM, value, &s->stops) != 0)
        return -1;
    return 0;
}

static int expand_stream(const Settings* s, FILE* in) {
    unsigned char buffer[65536];
    long long column = 0;
    int convert = 1;
    long got;

    while ((got = text_read_some(in, buffer, sizeof buffer)) > 0) {
        long i = 0;

        while (i < got) {
            unsigned char c = buffer[i];
            size_t n = 1;

            if (c >= 0xC0 && (size_t)(got - i) >= (c >= 0xF0 ? 4u : c >= 0xE0 ? 3u : 2u))
                utf8_decode(buffer + i, (size_t)(got - i), &n);
            if (convert) {
                if (c == '\t') {
                    long long next = tabstops_next(&s->stops, column);

                    do {
                        putchar(' ');
                    } while (++column < next);
                    i++;
                    continue;
                }
                if (c == '\b') {
                    if (column > 0)
                        column--;
                } else {
                    column++;
                    if (!(c == ' ') && s->initial)
                        convert = 0;
                }
            }
            fwrite(buffer + i, 1, n, stdout);
            if (c == '\n') {
                column = 0;
                convert = 1;
            }
            i += (long)n;
        }
        fflush(stdout);
    }
    return got < 0 ? 1 : 0;
}

int main(int argc, char* argv[]) {
    Settings settings;
    const char** operands;
    int count, result, i, status = 0;

    memset(&settings, 0, sizeof settings);
    text_binary_streams();
    tabstops_init(&settings.stops);
    result = args_parse(&spec, argc, argv, on_option, &settings, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    tabstops_finish(&settings.stops);
    count = (int)text_default_operands(operands, (size_t)count);

    for (i = 0; i < count; i++) {
        FILE* in;

        if (text_open(PROGRAM, operands[i], 1, &in) != 0) {
            status = 1;
            continue;
        }
        if (expand_stream(&settings, in) != 0)
            status = 1;
        if (in != stdin)
            fclose(in);
    }
    tabstops_free(&settings.stops);
    return fflush(stdout) != 0 ? 1 : status;
}
