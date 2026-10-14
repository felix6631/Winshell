#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "textutil.h"
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "tail"

static void print_usage(void) {
    puts("Usage: tail [OPTION]... [FILE]...\n"
         "Print the last 10 lines of each FILE to standard output.\n"
         "With more than one FILE, precede each with a header giving the file name.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -c, --bytes=[+]NUM       output the last NUM bytes; or use -c +NUM to output\n"
         "                             starting with byte NUM of each file\n"
         "  -f, --follow             output appended data as the file grows\n"
         "  -n, --lines=[+]NUM       output the last NUM lines, instead of the last 10;\n"
         "                             or use -n +NUM to output starting with line NUM\n"
         "  -NUM                     the same as -n NUM\n"
         "  -q, --quiet, --silent    never output headers giving file names\n"
         "  -s, --sleep-interval=N   with -f, sleep for approximately N seconds (default 1.0)\n"
         "                             between iterations\n"
         "  -v, --verbose            always output headers giving file names\n"
         "      --help               display this help and exit\n"
         "      --version            output version information and exit\n"
         "\n"
         "NUM may have a multiplier suffix: b 512, k 1024, M 1024*1024, G, kB 1000, MB, GB.\n"
         "Lines end at a line feed; a CR in front of it stays with its line, so files with\n"
         "LF and with CR LF line endings are cut at the same places and are not changed.\n"
         "Stop -f with Ctrl+C.");
}

typedef struct {
    int       bytes;        // -c
    long long count;
    int       fromStart;    // +NUM: begin at line or byte NUM
} Mode;

/** Index in data where the last n lines begin. A last line without a line feed counts. */
static size_t last_lines_start(const unsigned char* data, size_t length, long long n) {
    size_t i = length;
    long long seen = 0;

    if (n <= 0)
        return length;
    if (i > 0 && data[i - 1] == '\n')
        i--;                        // the line feed that ends the last line
    while (i > 0) {
        if (data[i - 1] == '\n' && ++seen == n)
            return i;
        i--;
    }
    return 0;
}

/** Streams from the start, dropping the first (count - 1) lines or bytes: tail -n +K, tail -c +K. */
static int tail_from(FILE* in, const Mode* mode) {
    unsigned char buffer[65536];
    long long toSkip = mode->count > 0 ? mode->count - 1 : 0;

    for (;;) {
        long read = text_read_some(in, buffer, sizeof buffer);
        size_t got, start = 0, i;

        if (read <= 0)
            break;
        got = (size_t)read;
        if (toSkip > 0) {
            if (mode->bytes) {
                if ((long long)got <= toSkip) {
                    toSkip -= (long long)got;
                    continue;
                }
                start = (size_t)toSkip;
                toSkip = 0;
            } else {
                for (i = 0; i < got && toSkip > 0; i++)
                    if (buffer[i] == '\n')
                        toSkip--;
                if (toSkip > 0)
                    continue;
                start = i;
            }
        }
        if (fwrite(buffer + start, 1, got - start, stdout) != got - start)
            return -1;
        fflush(stdout);
    }
    return 0;
}

/**
 * Reads the end of a seekable file: blocks are prepended from the end until there are enough bytes, or
 * enough line feeds. Returns 0 with *data and *length, or -1 when out of memory.
 */
static int read_end_of_file(FILE* in, const Mode* mode, unsigned char** data, size_t* length) {
    __int64 position, wanted = mode->bytes ? mode->count : 0;
    const size_t block = 65536;

    _fseeki64(in, 0, SEEK_END);
    position = _ftelli64(in);
    *data = malloc(1);
    *length = 0;
    if (*data == NULL)
        return -1;

    while (position > 0) {
        size_t take = position < (__int64)block ? (size_t)position : block;
        unsigned char* grown;

        if (mode->bytes && (long long)*length >= wanted)
            break;
        position -= (__int64)take;
        _fseeki64(in, position, SEEK_SET);

        grown = malloc(*length + take + 1);
        if (grown == NULL) {
            free(*data);
            return -1;
        }
        if (fread(grown, 1, take, in) != take) {
            free(grown);
            break;
        }
        memcpy(grown + take, *data, *length);
        free(*data);
        *data = grown;
        *length += take;

        if (!mode->bytes) {
            long long newlines = 0;
            size_t i;
            for (i = 0; i < *length; i++)
                if ((*data)[i] == '\n')
                    newlines++;
            if (newlines > mode->count)
                break;
        }
    }
    return 0;
}

/**
 * The last lines or bytes of a stream. A file is read from its end, so that
 * a large file costs only what is shown; other input is read completely.
 */
static int tail_last(FILE* in, int seekable, const Mode* mode) {
    unsigned char* data = NULL;
    size_t length = 0, start;
    int result = 0;

    if (seekable) {
        if (read_end_of_file(in, mode, &data, &length) != 0)
            return -1;
    } else {
        data = text_slurp(in, &length);
        if (data == NULL) {
            fprintf(stderr, PROGRAM ": memory exhausted\n");
            return -1;
        }
    }

    if (mode->bytes)
        start = (unsigned long long)mode->count >= length ? 0 : length - (size_t)mode->count;
    else
        start = last_lines_start(data, length, mode->count);

    if (length > start && fwrite(data + start, 1, length - start, stdout) != length - start)
        result = -1;
    free(data);
    return result;
}

static int is_seekable(FILE* in) {
    DWORD type = GetFileType((HANDLE)_get_osfhandle(_fileno(in)));
    return type == FILE_TYPE_DISK;
}

/** -f: print what is appended to the files, until the program is stopped. */
static void follow(const char** names, FILE** files, int count, int headers, double interval) {
    __int64* positions = calloc((size_t)count, sizeof *positions);
    int last = -1, i;

    if (positions == NULL)
        return;
    for (i = 0; i < count; i++) {
        if (files[i] != NULL && files[i] != stdin) {
            _fseeki64(files[i], 0, SEEK_END);
            positions[i] = _ftelli64(files[i]);
        }
    }

    for (;;) {
        for (i = 0; i < count; i++) {
            __int64 size;
            if (files[i] == NULL || files[i] == stdin)
                continue;
            fflush(files[i]);
            _fseeki64(files[i], 0, SEEK_END);
            size = _ftelli64(files[i]);

            if (size < positions[i]) {
                fprintf(stderr, PROGRAM ": %s: file truncated\n", names[i]);
                positions[i] = 0;
            }
            if (size > positions[i]) {
                unsigned char buffer[65536];
                size_t got;

                if (headers && last != i)
                    printf("%s==> %s <==\n", last >= 0 ? "\n" : "", names[i]);
                last = i;

                _fseeki64(files[i], positions[i], SEEK_SET);
                while ((got = fread(buffer, 1, sizeof buffer, files[i])) > 0)
                    fwrite(buffer, 1, got, stdout);
                clearerr(files[i]);
                positions[i] = _ftelli64(files[i]);
                fflush(stdout);
            }
        }
        Sleep((DWORD)(interval * 1000.0));
    }
}

enum { OPT_FOLLOW = 1, OPT_QUIET, OPT_VERBOSE, OPT_LINES, OPT_BYTES, OPT_SLEEP };

static const ArgsOption options[] = {
    { OPT_FOLLOW,  'f', "follow",         ARGS_NONE },
    { OPT_QUIET,   'q', "quiet",          ARGS_NONE },
    { OPT_QUIET,   0,   "silent",         ARGS_NONE },
    { OPT_VERBOSE, 'v', "verbose",        ARGS_NONE },
    { OPT_LINES,   'n', "lines",          ARGS_REQUIRED },
    { OPT_BYTES,   'c', "bytes",          ARGS_REQUIRED },
    { OPT_SLEEP,   's', "sleep-interval", ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 1 };

typedef struct {
    Mode   mode;
    int    quiet, verbose, follow;
    double interval;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;
    TextCount count;
    int bytes = id == OPT_BYTES;

    switch (id) {
        case OPT_FOLLOW:  o->follow = 1; return 0;
        case OPT_QUIET:   o->quiet = 1; o->verbose = 0; return 0;
        case OPT_VERBOSE: o->verbose = 1; o->quiet = 0; return 0;
        case OPT_SLEEP:
            o->interval = atof(value);
            if (o->interval <= 0)
                o->interval = 1.0;
            return 0;
        case ARGS_NUMBER: // -NUM is -n NUM
            if (text_parse_count(value, &count) != 0) {
                fprintf(stderr, PROGRAM ": invalid number of lines: '%s'\n", value);
                return -1;
            }
            o->mode.bytes = 0;
            o->mode.count = count.value;
            o->mode.fromStart = 0;
            return 0;
        default:
            if (text_parse_count(value, &count) != 0) {
                fprintf(stderr, PROGRAM ": invalid number of %s: '%s'\n", bytes ? "bytes" : "lines", value);
                return -1;
            }
            o->mode.bytes = bytes;
            o->mode.count = count.value;
            o->mode.fromStart = count.plus;
            return 0;
    }
}

int main(int argc, char* argv[]) {
    Options o = { { 0, 10, 0 }, 0, 0, 0, 1.0 };
    Mode mode;
    int quiet, verbose, follow_, status = 0, operandCount, i, result;
    double interval;
    const char** operands;
    FILE** streams;
    int headers, shown = 0, openCount = 0;

    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &operandCount);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    mode = o.mode;
    quiet = o.quiet;
    verbose = o.verbose;
    follow_ = o.follow;
    interval = o.interval;

    text_binary_streams();

    operandCount = (int)text_default_operands(operands, (size_t)operandCount);

    streams = calloc((size_t)operandCount, sizeof *streams);
    if (streams == NULL)
        return 1;
    headers = (operandCount > 1 && !quiet) || verbose;

    for (i = 0; i < operandCount; i++) {
        FILE* in;
        int result;

        if (text_open(PROGRAM, operands[i], 0, &in) != 0) {
            status = 1;
            continue;
        }

        if (headers) {
            printf("%s==> %s <==\n", shown ? "\n" : "", strcmp(operands[i], "-") == 0 ? "standard input" : operands[i]);
            shown = 1;
        }

        if (mode.fromStart)
            result = tail_from(in, &mode);
        else
            result = tail_last(in, in != stdin && is_seekable(in), &mode);

        if (result != 0) {
            fprintf(stderr, PROGRAM ": write error\n");
            status = 1;
        }

        if (follow_ && in != stdin) {
            streams[i] = in;
            openCount++;
        } else if (in != stdin) {
            fclose(in);
        } else {
            clearerr(stdin);
        }
    }

    fflush(stdout);
    if (follow_ && openCount > 0)
        follow(operands, streams, operandCount, headers, interval);

    free(streams);
    free(operands);
    return status;
}
