#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#include "vpath.h"
#include "textutil.h"
#include "args.h"

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

/** What cat keeps while it copies: the options, and the state that continues across files (the input is one stream). */
typedef struct {
    Options            opts;
    int                atLineStart;
    int                pendingCR;       // a CR at the start of a line, undecided whether CRLF follows
    int                blankRun;        // number of empty lines just seen
    unsigned long long lineNumber;
} Cat;

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

static void emit(int c) {
    putchar(c);
}

/** Writes one byte of a line, converting it when -T or -v ask for it. */
static void emit_char(Cat* x, unsigned char c) {
    if (c == '\t') {
        if (x->opts.showTabs) {
            emit('^');
            emit('I');
        } else {
            emit(c);
        }
        return;
    }

    if (x->opts.showNonprinting) {
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

static void print_number(Cat* x) {
    printf("%6llu\t", ++x->lineNumber);
}

/** Starts a line that is not empty. */
static void begin_line(Cat* x) {
    x->blankRun = 0;
    x->atLineStart = 0;
    if (x->opts.number || x->opts.numberNonblank)
        print_number(x);
}

/**
 * Ends a line. crlf tells that it was ended by CR LF.
 *
 * Windows adaptation: a CR LF pair is one line ending. -E puts the $ in
 * front of it ("a$" CR LF) instead of between the CR and the LF, and the
 * CR is only spelled out as ^M when -v asks for nonprinting characters,
 * which is what GNU cat shows for such a file.
 */
static void write_eol(Cat* x, int crlf) {
    if (crlf && x->opts.showNonprinting) {
        emit('^');
        emit('M');
    }
    if (x->opts.showEnds)
        emit('$');
    if (crlf && !x->opts.showNonprinting)
        emit('\r');
    emit('\n');
}

/** An empty line. */
static void blank_line(Cat* x, int crlf) {
    x->blankRun++;
    if (x->opts.squeeze && x->blankRun > 1)
        return;
    if (x->opts.number && !x->opts.numberNonblank)
        print_number(x);
    write_eol(x, crlf);
}

static void process_char(Cat* x, unsigned char c) {
    int crlf = 0;

    if (x->pendingCR) {
        x->pendingCR = 0;
        if (c == '\n') {
            crlf = 1;
        } else {
            // a CR on its own is an ordinary character
            if (x->atLineStart)
                begin_line(x);
            emit_char(x, '\r');
        }
    }

    if (c == '\r') {
        x->pendingCR = 1;
        return;
    }

    if (c == '\n') {
        if (x->atLineStart) {
            blank_line(x, crlf);
        } else {
            write_eol(x, crlf);
            x->atLineStart = 1;
        }
        return;
    }

    if (x->atLineStart)
        begin_line(x);
    emit_char(x, c);
}

static void finish(Cat* x) {
    if (x->pendingCR) {
        x->pendingCR = 0;
        if (x->atLineStart)
            begin_line(x);
        emit_char(x, '\r');
    }
}

/** Copies one stream to standard output. Returns 0, or -1 after a read error. */
static int copy_stream(Cat* x, FILE* in, int plain) {
    static unsigned char buffer[65536];
    long got;
    size_t length, i;

    // what is there is passed on at once, so that "tail -f log | cat" shows lines as they come
    while ((got = text_read_some(in, buffer, sizeof buffer)) > 0) {
        length = (size_t)got;
        if (plain) {
            if (fwrite(buffer, 1, length, stdout) != length)
                return -1;
        } else {
            for (i = 0; i < length; i++)
                process_char(x, buffer[i]);
        }
        fflush(stdout);
    }
    return got < 0 ? -1 : 0;
}

/** Windows reports a directory opened as a file as "Permission denied"; Linux says what it is. */
static const char* open_error(const char* path) {
    DWORD attributes = GetFileAttributesA(path);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return "Is a directory";
    return strerror(errno);
}

static int cat_file(Cat* x, const char* path, int plain) {
    FILE* in;
    int status;

    if (strcmp(path, "-") == 0) {
        in = stdin;
    } else {
        // paths are those of the virtual file system: /c/Users/me/file
        char native[VPATH_MAX];
        int kind = vpath_to_native(path, native, sizeof native);

        if (kind == VPATH_ROOT) {
            fprintf(stderr, PROGRAM ": %s: Is a directory\n", path);
            return -1;
        }
        if (kind != 0) {
            fprintf(stderr, PROGRAM ": %s: %s\n", path, strerror(errno));
            return -1;
        }

        in = fopen(native, "rb");
        if (in == NULL) {
            fprintf(stderr, PROGRAM ": %s: %s\n", path, open_error(native));
            return -1;
        }
    }

    status = copy_stream(x, in, plain);
    if (status != 0)
        fprintf(stderr, PROGRAM ": %s: %s\n", path, strerror(errno));

    if (in != stdin)
        fclose(in);
    else
        clearerr(stdin);
    return status;
}

enum { OPT_ALL = 1, OPT_NUMBER_NONBLANK, OPT_E, OPT_ENDS, OPT_NUMBER, OPT_SQUEEZE, OPT_T, OPT_TABS,
       OPT_IGNORED, OPT_NONPRINTING };

static const ArgsOption options[] = {
    { OPT_ALL,             'A', "show-all",         ARGS_NONE },
    { OPT_NUMBER_NONBLANK, 'b', "number-nonblank",  ARGS_NONE },
    { OPT_E,               'e', NULL,               ARGS_NONE },
    { OPT_ENDS,            'E', "show-ends",        ARGS_NONE },
    { OPT_NUMBER,          'n', "number",           ARGS_NONE },
    { OPT_SQUEEZE,         's', "squeeze-blank",    ARGS_NONE },
    { OPT_T,               't', NULL,               ARGS_NONE },
    { OPT_TABS,            'T', "show-tabs",        ARGS_NONE },
    { OPT_IGNORED,         'u', NULL,               ARGS_NONE },
    { OPT_NONPRINTING,     'v', "show-nonprinting", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    Cat* x = context;
    (void)value;
    switch (id) {
        case OPT_ALL:             x->opts.showNonprinting = x->opts.showEnds = x->opts.showTabs = 1; break;
        case OPT_NUMBER_NONBLANK: x->opts.numberNonblank = 1; break;
        case OPT_E:               x->opts.showNonprinting = x->opts.showEnds = 1; break;
        case OPT_ENDS:            x->opts.showEnds = 1; break;
        case OPT_NUMBER:          x->opts.number = 1; break;
        case OPT_SQUEEZE:         x->opts.squeeze = 1; break;
        case OPT_T:               x->opts.showNonprinting = x->opts.showTabs = 1; break;
        case OPT_TABS:            x->opts.showTabs = 1; break;
        case OPT_NONPRINTING:     x->opts.showNonprinting = 1; break;
        default: break;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    const char** files;
    int fileCount, status = 0, i, plain, result;
    Cat state;
    Cat* x = &state;

    memset(x, 0, sizeof *x);
    x->atLineStart = 1;

    result = args_parse(&spec, argc, argv, on_option, x, &files, &fileCount);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    plain = !(x->opts.number || x->opts.numberNonblank || x->opts.squeeze ||
              x->opts.showEnds || x->opts.showTabs || x->opts.showNonprinting);

    if (fileCount == 0)
        files[fileCount++] = "-";

    for (i = 0; i < fileCount; i++) {
        if (cat_file(x, files[i], plain) != 0)
            status = 1;
    }
    finish(x);

    if (fflush(stdout) != 0) {
        fprintf(stderr, PROGRAM ": write error: %s\n", strerror(errno));
        status = 1;
    }
    free(files);
    return status;
}
