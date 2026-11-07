/*
 * unexpand: converts spaces to tabs, as GNU unexpand does. By default only the blanks at the start of a line;
 * with -a (and with -t) every run of blanks that ends at a tab stop. A single blank in front of a tab stop
 * stays a blank unless more blanks follow it up to the next stop.
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
#define PROGRAM "unexpand"

static void print_usage(void) {
    puts("Usage: unexpand [OPTION]... [FILE]...\n"
         "Convert blanks in each FILE to tabs, writing to standard output.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -a, --all        convert all blanks, instead of just initial blanks\n"
         "      --first-only  convert only leading sequences of blanks (overrides -a)\n"
         "  -t, --tabs=N     have tabs N characters apart instead of 8 (enables -a)\n"
         "  -t, --tabs=LIST  use comma separated LIST of tab positions (enables -a).\n"
         "                     The last specified position can be prefixed with '/'\n"
         "                     to specify a tab size to use after the last\n"
         "                     explicitly specified tab stop.  Also a prefix of '+'\n"
         "                     can be used to align remaining tab stops relative to\n"
         "                     the last specified tab stop instead of the first column\n"
         "      --help       display this help and exit\n"
         "      --version    output version information and exit");
}

enum { OPT_ALL = 1, OPT_FIRST_ONLY, OPT_TABS };

static const ArgsOption options[] = {
    { OPT_ALL,        'a', "all",        ARGS_NONE },
    { OPT_FIRST_ONLY, 0,   "first-only", ARGS_NONE },
    { OPT_TABS,       't', "tabs",       ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int      all;
    int      firstOnly;
    TabStops stops;
} Settings;

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;

    if (id == OPT_ALL) {
        s->all = 1;
    } else if (id == OPT_FIRST_ONLY) {
        s->firstOnly = 1;                   // wins over -a and -t, whatever the order
    } else {
        s->all = 1;
        if (tabstops_add(PROGRAM, value, &s->stops) != 0)
            return -1;
    }
    return 0;
}

/** The state of the line being converted. */
typedef struct {
    int       convert;                  // blanks are still being looked at
    int       oneBlankBeforeStop;       // a single blank stands just before a tab stop
    int       prevBlank;
    long long column;
    unsigned char pending[4096];        // the blanks that may become tabs (their number is `pending count`)
    size_t    pendingCount;
} State;

static void reset_line(State* st) {
    st->convert = 1;
    st->oneBlankBeforeStop = 0;
    st->prevBlank = 1;
    st->column = 0;
    st->pendingCount = 0;
}

static void flush_pending(State* st) {
    // a single blank just before a tab stop becomes a tab when more blanks follow it
    if (st->pendingCount > 1 && st->oneBlankBeforeStop)
        st->pending[0] = '\t';
    if (st->pendingCount > 0)
        fwrite(st->pending, 1, st->pendingCount, stdout);
    st->pendingCount = 0;
}

static int unexpand_stream(const Settings* s, FILE* in, State* st) {
    unsigned char buffer[65536];
    long got;

    while ((got = text_read_some(in, buffer, sizeof buffer)) > 0) {
        long i = 0;

        while (i < got) {
            unsigned char c = buffer[i];
            size_t n = 1;
            int blank = c == ' ' || c == '\t';

            if (c >= 0xC0 && (size_t)(got - i) >= (c >= 0xF0 ? 4u : c >= 0xE0 ? 3u : 2u))
                utf8_decode(buffer + i, (size_t)(got - i), &n);

            if (st->convert) {
                if (blank && tabstops_beyond(&s->stops, st->column)) {
                    // past the last tab stop of a list: nothing more is converted on this line
                    flush_pending(st);
                    st->convert = 0;
                    fwrite(buffer + i, 1, n, stdout);
                    i += (long)n;
                    continue;
                }
                if (blank) {
                    long long next = tabstops_next(&s->stops, st->column);

                    if (c == '\t') {
                        st->column = next;
                        if (st->pendingCount > 0)
                            st->pending[0] = '\t';
                    } else {
                        st->column++;
                        if (!(st->prevBlank && st->column == next)) {
                            // not yet known whether the blanks will be replaced by tabs
                            if (st->column == next)
                                st->oneBlankBeforeStop = 1;
                            if (st->pendingCount < sizeof st->pending)
                                st->pending[st->pendingCount++] = c;
                            st->prevBlank = 1;
                            i++;
                            continue;
                        }
                        // the blanks up to the stop become a tab (or two)
                        if (st->pendingCount == 0)
                            st->pending[0] = '\t';
                        else
                            st->pending[0] = '\t';
                        c = '\t';
                    }
                    // the pending blanks go, unless it was a single blank just before a tab stop
                    st->pendingCount = st->oneBlankBeforeStop ? 1 : 0;
                } else if (c == '\b') {
                    if (st->column > 0)
                        st->column--;
                } else {
                    st->column++;
                }
                flush_pending(st);
                st->prevBlank = blank;
                st->oneBlankBeforeStop = 0;
            }

            if (n == 1)
                putchar(c);
            else
                fwrite(buffer + i, 1, n, stdout);
            if (c == '\n') {
                reset_line(st);
            } else if (!s->all && !blank) {
                st->convert = 0;
            }
            i += (long)n;
        }
        fflush(stdout);
    }
    return got < 0 ? 1 : 0;
}

int main(int argc, char* argv[]) {
    Settings settings;
    State state;
    const char** operands;
    int count, result, i, status = 0;

    memset(&settings, 0, sizeof settings);
    text_binary_streams();
    tabstops_init(&settings.stops);
    result = args_parse(&spec, argc, argv, on_option, &settings, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    tabstops_finish(&settings.stops);
    if (settings.firstOnly)
        settings.all = 0;
    count = (int)text_default_operands(operands, (size_t)count);

    reset_line(&state);
    for (i = 0; i < count; i++) {
        FILE* in;

        if (text_open(PROGRAM, operands[i], 1, &in) != 0) {
            status = 1;
            continue;
        }
        if (unexpand_stream(&settings, in, &state) != 0)
            status = 1;
        if (in != stdin)
            fclose(in);
    }
    flush_pending(&state);
    tabstops_free(&settings.stops);
    return fflush(stdout) != 0 ? 1 : status;
}
