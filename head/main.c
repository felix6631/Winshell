#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "head"

static void print_usage(void) {
    puts("Usage: head [OPTION]... [FILE]...\n"
         "Print the first 10 lines of each FILE to standard output.\n"
         "With more than one FILE, precede each with a header giving the file name.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -c, --bytes=[-]NUM       print the first NUM bytes of each file;\n"
         "                             with the leading '-', print all but the last NUM bytes\n"
         "  -n, --lines=[-]NUM       print the first NUM lines instead of the first 10;\n"
         "                             with the leading '-', print all but the last NUM lines\n"
         "  -NUM                     the same as -n NUM\n"
         "  -q, --quiet, --silent    never print headers giving file names\n"
         "  -v, --verbose            always print headers giving file names\n"
         "      --help               display this help and exit\n"
         "      --version            output version information and exit\n"
         "\n"
         "NUM may have a multiplier suffix: b 512, k 1024, M 1024*1024, G, kB 1000, MB, GB.\n"
         "Lines end at a line feed; a CR in front of it stays with its line, so files with\n"
         "LF and with CR LF line endings are cut at the same places and are not changed.");
}

typedef struct {
    int       bytes;        // -c: count bytes, not lines
    long long count;        // how many
    int       fromEnd;      // -n -K / -c -K: everything but the last ones
} Mode;

/** Writes the first n lines (or bytes) of the stream. Returns 0, or -1 on a write error. */
static int head_first(FILE* in, const Mode* mode) {
    unsigned char buffer[65536];
    long long remaining = mode->count;

    while (remaining > 0) {
        size_t want = sizeof buffer, got, i, take;
        long read;

        if (mode->bytes && (long long)want > remaining)
            want = (size_t)remaining;
        // what is there, not more: the lines wanted may be all that will ever come
        read = text_read_some(in, buffer, want);
        if (read <= 0)
            break;
        got = (size_t)read;

        take = got;
        if (mode->bytes) {
            remaining -= (long long)got;
        } else {
            for (i = 0; i < got; i++) {
                if (buffer[i] == '\n' && --remaining == 0) {
                    take = i + 1;
                    break;
                }
            }
        }
        if (fwrite(buffer, 1, take, stdout) != take)
            return -1;
        fflush(stdout);
        if (take < got)
            break;
    }
    return 0;
}

/** Writes all but the last n lines (or bytes). The input is read completely first. */
static int head_all_but_last(FILE* in, const Mode* mode) {
    size_t length = 0, keep, i;
    unsigned char* data = text_slurp(in, &length);

    if (data == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return -1;
    }

    if (mode->bytes) {
        keep = (unsigned long long)mode->count >= length ? 0 : length - (size_t)mode->count;
    } else {
        // the number of lines; a last line without a line feed counts as well
        long long lines = 0, wanted;
        for (i = 0; i < length; i++)
            if (data[i] == '\n')
                lines++;
        if (length > 0 && data[length - 1] != '\n')
            lines++;
        wanted = lines - mode->count;

        keep = 0;
        if (wanted > 0) {
            long long seen = 0;
            keep = length;      // all of it, when the wanted lines reach the end
            for (i = 0; i < length; i++) {
                if (data[i] == '\n' && ++seen == wanted) {
                    keep = i + 1;
                    break;
                }
            }
        }
    }

    if (keep > 0 && fwrite(data, 1, keep, stdout) != keep) {
        free(data);
        return -1;
    }
    free(data);
    return 0;
}

enum { OPT_QUIET = 1, OPT_VERBOSE, OPT_LINES, OPT_BYTES };

static const ArgsOption options[] = {
    { OPT_QUIET,   'q', "quiet",   ARGS_NONE },
    { OPT_QUIET,   0,   "silent",  ARGS_NONE },
    { OPT_VERBOSE, 'v', "verbose", ARGS_NONE },
    { OPT_LINES,   'n', "lines",   ARGS_REQUIRED },
    { OPT_BYTES,   'c', "bytes",   ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 1 };

typedef struct {
    Mode mode;
    int  quiet, verbose;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;
    TextCount count;
    int bytes = id == OPT_BYTES;

    switch (id) {
        case OPT_QUIET:   o->quiet = 1; o->verbose = 0; return 0;
        case OPT_VERBOSE: o->verbose = 1; o->quiet = 0; return 0;
        case ARGS_NUMBER: // -NUM: the digits are a line count
            if (text_parse_count(value, &count) != 0) {
                fprintf(stderr, PROGRAM ": invalid number of lines: '%s'\n", value);
                return -1;
            }
            o->mode.bytes = 0;
            o->mode.count = count.value;
            o->mode.fromEnd = 0;
            return 0;
        default:
            if (text_parse_count(value, &count) != 0) {
                fprintf(stderr, PROGRAM ": invalid number of %s: '%s'\n", bytes ? "bytes" : "lines", value);
                return -1;
            }
            o->mode.bytes = bytes;
            o->mode.count = count.value;
            o->mode.fromEnd = count.minus;
            return 0;
    }
}

int main(int argc, char* argv[]) {
    Options o = { { 0, 10, 0 }, 0, 0 };
    Mode mode;
    int quiet, verbose, status = 0, files, shown = 0, i, result;
    const char** operands;

    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &files);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    mode = o.mode;
    quiet = o.quiet;
    verbose = o.verbose;

    text_binary_streams();

    if (files == 0)
        operands[files++] = "-";

    for (i = 0; i < files; i++) {
        FILE* in;
        int failed;

        if (text_open(PROGRAM, operands[i], 0, &in) != 0) {
            status = 1;
            continue;
        }

        if ((files > 1 && !quiet) || verbose) {
            printf("%s==> %s <==\n", shown ? "\n" : "", strcmp(operands[i], "-") == 0 ? "standard input" : operands[i]);
            shown = 1;
        }

        failed = mode.fromEnd ? head_all_but_last(in, &mode) : head_first(in, &mode);
        if (in != stdin)
            fclose(in);
        else
            clearerr(stdin);

        if (failed != 0) {
            fprintf(stderr, PROGRAM ": write error\n");
            status = 1;
            break;
        }
    }

    free(operands);
    return status;
}
