#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <windows.h>

#define VERSION "0.1"
#define PROGRAM "cat"

/*
 * Like on Linux, cat passes the bytes through unchanged: the standard
 * streams and the files are all read and written in binary mode.
 *
 * Windows adaptation: text files commonly end their lines with CR LF, so
 * for the purpose of -b and -s a line holding just CR LF counts as blank,
 * the same as a line holding just LF.
 */

typedef struct {
    int number;         // -n  number all output lines
    int numberNonblank; // -b  number nonempty output lines, overrides -n
    int squeeze;        // -s  suppress repeated empty output lines
    int showEnds;       // -E  display $ at end of each line
    int showTabs;       // -T  display TAB characters as ^I
    int showNonprinting;// -v  use ^ and M- notation, except for LFD and TAB
} Options;

static Options opts;

/* state that continues across files: cat treats its input as one stream */
static int atLineStart = 1;
static int pendingCR = 0;       // a CR at the start of a line, undecided whether CRLF follows
static int blankRun = 0;        // number of empty lines just seen
static unsigned long long lineNumber = 0;

static void print_usage(void) {
    puts("Usage: cat [OPTION]... [FILE]...\n"
         "Concatenate FILE(s) to standard output.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -A, --show-all           equivalent to -vET\n"
         "  -b, --number-nonblank    number nonempty output lines, overrides -n\n"
         "  -e                       equivalent to -vE\n"
         "  -E, --show-ends          display $ at end of each line\n"
         "  -n, --number             number all output lines\n"
         "  -s, --squeeze-blank      suppress repeated empty output lines\n"
         "  -t                       equivalent to -vT\n"
         "  -T, --show-tabs          display TAB characters as ^I\n"
         "  -u                       (ignored)\n"
         "  -v, --show-nonprinting   use ^ and M- notation, except for LFD and TAB\n"
         "      --help               display this help and exit\n"
         "      --version            output version information and exit\n"
         "\n"
         "Examples:\n"
         "  cat f - g  Output f's contents, then standard input, then g's contents.\n"
         "  cat        Copy standard input to standard output.");
}

static void print_try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

static void emit(int c) {
    putchar(c);
}

/** Writes one byte of a line, converting it when -T or -v ask for it. */
static void emit_char(unsigned char c) {
    if (c == '\t') {
        if (opts.showTabs) {
            emit('^');
            emit('I');
        } else {
            emit(c);
        }
        return;
    }

    if (opts.showNonprinting) {
        if (c >= 128) {
            emit('M');
            emit('-');
            c -= 128;
        }
        if (c < 32) {
            emit('^');
            emit(c + 64);
            return;
        }
        if (c == 127) {
            emit('^');
            emit('?');
            return;
        }
    }
    emit(c);
}

static void print_number(void) {
    printf("%6llu\t", ++lineNumber);
}

/** Starts a line that is not empty. */
static void begin_line(void) {
    blankRun = 0;
    atLineStart = 0;
    if (opts.number || opts.numberNonblank)
        print_number();
}

/**
 * Ends a line. crlf tells that it was ended by CR LF.
 *
 * Windows adaptation: a CR LF pair is one line ending. -E puts the $ in
 * front of it ("a$" CR LF) instead of between the CR and the LF, and the
 * CR is only spelled out as ^M when -v asks for nonprinting characters,
 * which is what GNU cat shows for such a file.
 */
static void write_eol(int crlf) {
    if (crlf && opts.showNonprinting) {
        emit('^');
        emit('M');
    }
    if (opts.showEnds)
        emit('$');
    if (crlf && !opts.showNonprinting)
        emit('\r');
    emit('\n');
}

/** An empty line. */
static void blank_line(int crlf) {
    blankRun++;
    if (opts.squeeze && blankRun > 1)
        return;
    if (opts.number && !opts.numberNonblank)
        print_number();
    write_eol(crlf);
}

static void process_char(unsigned char c) {
    int crlf = 0;

    if (pendingCR) {
        pendingCR = 0;
        if (c == '\n') {
            crlf = 1;
        } else {
            // a CR on its own is an ordinary character
            if (atLineStart)
                begin_line();
            emit_char('\r');
        }
    }

    if (c == '\r') {
        pendingCR = 1;
        return;
    }

    if (c == '\n') {
        if (atLineStart) {
            blank_line(crlf);
        } else {
            write_eol(crlf);
            atLineStart = 1;
        }
        return;
    }

    if (atLineStart)
        begin_line();
    emit_char(c);
}

static void finish(void) {
    if (pendingCR) {
        pendingCR = 0;
        if (atLineStart)
            begin_line();
        emit_char('\r');
    }
}

/** Copies one stream to standard output. Returns 0, or -1 after a read error. */
static int copy_stream(FILE* in, int plain) {
    static unsigned char buffer[65536];
    size_t length, i;

    while ((length = fread(buffer, 1, sizeof buffer, in)) > 0) {
        if (plain) {
            if (fwrite(buffer, 1, length, stdout) != length)
                return -1;
        } else {
            for (i = 0; i < length; i++)
                process_char(buffer[i]);
        }
    }
    return ferror(in) ? -1 : 0;
}

/** Windows reports a directory opened as a file as "Permission denied"; Linux says what it is. */
static const char* open_error(const char* path) {
    DWORD attributes = GetFileAttributesA(path);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return "Is a directory";
    return strerror(errno);
}

static int cat_file(const char* path, int plain) {
    FILE* in;
    int status;

    if (strcmp(path, "-") == 0) {
        in = stdin;
    } else {
        in = fopen(path, "rb");
        if (in == NULL) {
            fprintf(stderr, PROGRAM ": %s: %s\n", path, open_error(path));
            return -1;
        }
    }

    status = copy_stream(in, plain);
    if (status != 0)
        fprintf(stderr, PROGRAM ": %s: %s\n", path, strerror(errno));

    if (in != stdin)
        fclose(in);
    else
        clearerr(stdin);
    return status;
}

int main(int argc, char* argv[]) {
    const char** files = malloc((size_t)(argc + 1) * sizeof *files);
    int fileCount = 0, optionsEnded = 0, status = 0, i, plain;

    if (files == NULL)
        return 1;

    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (optionsEnded || arg[0] != '-' || arg[1] == '\0') {
            files[fileCount++] = arg;
            continue;
        }

        if (strcmp(arg, "--") == 0) {
            optionsEnded = 1;
        } else if (arg[1] == '-') {
            if (strcmp(arg, "--help") == 0) {
                print_usage();
                return 0;
            } else if (strcmp(arg, "--version") == 0) {
                puts(PROGRAM " (Winshell) " VERSION);
                return 0;
            } else if (strcmp(arg, "--show-all") == 0) {
                opts.showNonprinting = opts.showEnds = opts.showTabs = 1;
            } else if (strcmp(arg, "--number-nonblank") == 0) {
                opts.numberNonblank = 1;
            } else if (strcmp(arg, "--show-ends") == 0) {
                opts.showEnds = 1;
            } else if (strcmp(arg, "--number") == 0) {
                opts.number = 1;
            } else if (strcmp(arg, "--squeeze-blank") == 0) {
                opts.squeeze = 1;
            } else if (strcmp(arg, "--show-tabs") == 0) {
                opts.showTabs = 1;
            } else if (strcmp(arg, "--show-nonprinting") == 0) {
                opts.showNonprinting = 1;
            } else {
                fprintf(stderr, PROGRAM ": unrecognized option '%s'\n", arg);
                print_try_help();
                return 1;
            }
        } else {
            const char* p;
            for (p = arg + 1; *p != '\0'; p++) {
                switch (*p) {
                    case 'A': opts.showNonprinting = opts.showEnds = opts.showTabs = 1; break;
                    case 'b': opts.numberNonblank = 1; break;
                    case 'e': opts.showNonprinting = opts.showEnds = 1; break;
                    case 'E': opts.showEnds = 1; break;
                    case 'n': opts.number = 1; break;
                    case 's': opts.squeeze = 1; break;
                    case 't': opts.showNonprinting = opts.showTabs = 1; break;
                    case 'T': opts.showTabs = 1; break;
                    case 'u': break;
                    case 'v': opts.showNonprinting = 1; break;
                    default:
                        fprintf(stderr, PROGRAM ": invalid option -- '%c'\n", *p);
                        print_try_help();
                        return 1;
                }
            }
        }
    }

    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    plain = !(opts.number || opts.numberNonblank || opts.squeeze ||
              opts.showEnds || opts.showTabs || opts.showNonprinting);

    if (fileCount == 0)
        files[fileCount++] = "-";

    for (i = 0; i < fileCount; i++) {
        if (cat_file(files[i], plain) != 0)
            status = 1;
    }
    finish();

    if (fflush(stdout) != 0) {
        fprintf(stderr, PROGRAM ": write error: %s\n", strerror(errno));
        status = 1;
    }
    free(files);
    return status;
}
