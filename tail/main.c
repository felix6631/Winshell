#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "textutil.h"

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

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
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

/** Reads everything. The result has to be freed. */
static unsigned char* slurp(FILE* in, size_t* length) {
    size_t capacity = 65536, used = 0;
    unsigned char* data = malloc(capacity);

    while (data != NULL) {
        size_t got;
        if (used == capacity) {
            unsigned char* grown = realloc(data, capacity * 2);
            if (grown == NULL) {
                free(data);
                data = NULL;
                break;
            }
            data = grown;
            capacity *= 2;
        }
        got = fread(data + used, 1, capacity - used, in);
        if (got == 0)
            break;
        used += got;
    }
    *length = used;
    return data;
}

/** Streams from the start, dropping the first (count - 1) lines or bytes: tail -n +K, tail -c +K. */
static int tail_from(FILE* in, const Mode* mode) {
    unsigned char buffer[65536];
    long long toSkip = mode->count > 0 ? mode->count - 1 : 0;

    for (;;) {
        size_t got = fread(buffer, 1, sizeof buffer, in), start = 0, i;

        if (got == 0)
            break;
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
        __int64 size, position, wanted;
        size_t block = 65536;

        _fseeki64(in, 0, SEEK_END);
        size = _ftelli64(in);
        position = size;
        data = malloc(1);
        if (data == NULL)
            return -1;
        wanted = mode->bytes ? mode->count : 0;

        // prepend blocks from the end until enough bytes, or enough line feeds, are there
        for (;;) {
            size_t take;
            unsigned char* grown;
            long long newlines = 0;
            size_t i;

            if (position == 0)
                break;
            if (mode->bytes ? (long long)length >= wanted : 0)
                break;
            take = position < (__int64)block ? (size_t)position : block;
            position -= (__int64)take;
            _fseeki64(in, position, SEEK_SET);

            grown = malloc(length + take + 1);
            if (grown == NULL) {
                free(data);
                return -1;
            }
            if (fread(grown, 1, take, in) != take) {
                free(grown);
                break;
            }
            memcpy(grown + take, data, length);
            free(data);
            data = grown;
            length += take;

            if (!mode->bytes) {
                for (i = 0; i < length; i++)
                    if (data[i] == '\n')
                        newlines++;
                if (newlines > mode->count)
                    break;
            }
        }
    } else {
        data = slurp(in, &length);
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

int main(int argc, char* argv[]) {
    Mode mode = { 0, 10, 0 };
    int quiet = 0, verbose = 0, follow_ = 0, optionsEnded = 0, status = 0, operandCount = 0, i;
    double interval = 1.0;
    const char** operands = malloc((size_t)(argc + 1) * sizeof *operands);
    FILE** streams;
    int headers, shown = 0, openCount = 0;

    if (operands == NULL)
        return 1;

    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (optionsEnded || arg[0] != '-' || arg[1] == '\0') {
            // "+5" is no option, but the argument of -n when it comes alone as -n +5; handled below
            operands[operandCount++] = arg;
        } else if (strcmp(arg, "--") == 0) {
            optionsEnded = 1;
        } else if (arg[1] == '-') {
            const char* value = NULL;
            int bytes = 0, isCount = 0;

            if (strcmp(arg, "--help") == 0) {
                print_usage();
                return 0;
            } else if (strcmp(arg, "--version") == 0) {
                puts(PROGRAM " (Winshell) " VERSION);
                return 0;
            } else if (strcmp(arg, "--follow") == 0) {
                follow_ = 1;
            } else if (strcmp(arg, "--quiet") == 0 || strcmp(arg, "--silent") == 0) {
                quiet = 1;
                verbose = 0;
            } else if (strcmp(arg, "--verbose") == 0) {
                verbose = 1;
                quiet = 0;
            } else if (strncmp(arg, "--lines=", 8) == 0) {
                value = arg + 8;
                isCount = 1;
            } else if (strncmp(arg, "--bytes=", 8) == 0) {
                value = arg + 8;
                isCount = bytes = 1;
            } else if (strncmp(arg, "--sleep-interval=", 17) == 0) {
                interval = atof(arg + 17);
                if (interval <= 0)
                    interval = 1.0;
            } else {
                fprintf(stderr, PROGRAM ": unrecognized option '%s'\n", arg);
                try_help();
                return 1;
            }

            if (isCount) {
                TextCount count;
                if (text_parse_count(value, &count) != 0) {
                    fprintf(stderr, PROGRAM ": invalid number of %s: '%s'\n", bytes ? "bytes" : "lines", value);
                    return 1;
                }
                mode.bytes = bytes;
                mode.count = count.value;
                mode.fromStart = count.plus;
            }
        } else {
            const char* p;

            // -NUM is -n NUM
            if (arg[1] >= '0' && arg[1] <= '9') {
                TextCount count;
                if (text_parse_count(arg + 1, &count) != 0) {
                    fprintf(stderr, PROGRAM ": invalid number of lines: '%s'\n", arg + 1);
                    return 1;
                }
                mode.bytes = 0;
                mode.count = count.value;
                mode.fromStart = 0;
                continue;
            }

            for (p = arg + 1; *p != '\0'; p++) {
                switch (*p) {
                    case 'f': follow_ = 1; break;
                    case 'q': quiet = 1; verbose = 0; break;
                    case 'v': verbose = 1; quiet = 0; break;
                    case 'n':
                    case 'c':
                    case 's': {
                        char option = *p;
                        const char* value;

                        if (p[1] != '\0') {
                            value = p + 1;
                        } else if (i + 1 < argc) {
                            value = argv[++i];
                        } else {
                            fprintf(stderr, PROGRAM ": option requires an argument -- '%c'\n", option);
                            try_help();
                            return 1;
                        }

                        if (option == 's') {
                            interval = atof(value);
                            if (interval <= 0)
                                interval = 1.0;
                        } else {
                            TextCount count;
                            if (text_parse_count(value, &count) != 0) {
                                fprintf(stderr, PROGRAM ": invalid number of %s: '%s'\n",
                                        option == 'c' ? "bytes" : "lines", value);
                                return 1;
                            }
                            mode.bytes = option == 'c';
                            mode.count = count.value;
                            mode.fromStart = count.plus;
                        }
                        p += strlen(p) - 1;
                        break;
                    }
                    default:
                        fprintf(stderr, PROGRAM ": invalid option -- '%c'\n", *p);
                        try_help();
                        return 1;
                }
            }
        }
    }

    text_binary_streams();

    if (operandCount == 0)
        operands[operandCount++] = "-";

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
