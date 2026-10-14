/*
 * xargs: builds command lines from standard input (or a file) and runs the command with them.
 *
 * Items are separated by blanks and line feeds, with quotes and backslashes (or by a delimiter, or by
 * NUL, or by lines with -I). The batches are cut by -n (arguments), -L (lines), -s (size of the
 * command line) and the command is run for each: up to -P of them at once.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "cmdline.h"
#include "program.h"
#include "textutil.h"
#include "vec.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "xargs"

// what CreateProcess accepts is 32767 characters; some is kept for the program name and the environment block
#define DEFAULT_MAX_CHARS 30000

static void print_usage(void) {
    puts("Usage: xargs [OPTION]... COMMAND [INITIAL-ARGS]...\n"
         "Run COMMAND with arguments INITIAL-ARGS and more arguments read from input.\n"
         "\n"
         "  -0, --null                   items are separated by a null, not whitespace;\n"
         "                                 disables quote and backslash processing\n"
         "  -a, --arg-file=FILE          read arguments from FILE, not standard input\n"
         "  -d, --delimiter=CHARACTER    items in input stream are separated by CHARACTER,\n"
         "                                 not by whitespace; disables quote and backslash\n"
         "                                 processing\n"
         "  -E END                       set logical EOF string; if END occurs as a line\n"
         "                                 of input, the rest of the input is ignored\n"
         "  -e, --eof[=END]              equivalent to -E END if END is specified;\n"
         "                                 otherwise, there is no end-of-file string\n"
         "  -I R                         same as --replace=R\n"
         "  -i, --replace[=R]            replace R in INITIAL-ARGS with names read\n"
         "                                 from standard input; if R is unspecified,\n"
         "                                 assume {}\n"
         "  -L, --max-lines=MAX-LINES    use at most MAX-LINES nonblank input lines per\n"
         "                                 command line\n"
         "  -l[MAX-LINES]                similar to -L but defaults to at most one non-\n"
         "                                 blank input line if MAX-LINES is not specified\n"
         "  -n, --max-args=MAX-ARGS      use at most MAX-ARGS arguments per command line\n"
         "  -P, --max-procs=MAX-PROCS    run at most MAX-PROCS processes at a time\n"
         "  -p, --interactive            prompt before running commands\n"
         "  -r, --no-run-if-empty        if there are no arguments, then do not run COMMAND;\n"
         "                                 if this option is not given, COMMAND will be\n"
         "                                 run once even if there is no input\n"
         "  -s, --max-chars=MAX-CHARS    limit length of command line to MAX-CHARS\n"
         "  -t, --verbose                print commands before executing them\n"
         "  -x, --exit                   exit if the size (see -s) is exceeded\n"
         "      --show-limits            show limits on command-line length\n"
         "      --help                   display this help and exit\n"
         "      --version                output version information and exit\n"
         "\n"
         "If COMMAND is not given, echo is used. The exit status is 0 if all runs\n"
         "succeeded, 123 if any exited with 1-125, 124 if one exited with 255, 126 if\n"
         "COMMAND cannot be run, 127 if it is not found, and 1 for other errors.");
}

enum {
    OPT_NULL = 1, OPT_ARG_FILE, OPT_DELIMITER, OPT_EOF_STR, OPT_EOF_OPT, OPT_REPLACE_STR, OPT_REPLACE_OPT,
    OPT_MAX_LINES, OPT_MAX_LINES_OPT, OPT_MAX_ARGS, OPT_MAX_PROCS, OPT_INTERACTIVE, OPT_NO_RUN, OPT_MAX_CHARS,
    OPT_VERBOSE, OPT_EXIT, OPT_SHOW_LIMITS, OPT_ACCEPTED
};

static const ArgsOption options[] = {
    { OPT_NULL,         '0', "null",              ARGS_NONE },
    { OPT_ARG_FILE,     'a', "arg-file",          ARGS_REQUIRED },
    { OPT_DELIMITER,    'd', "delimiter",         ARGS_REQUIRED },
    { OPT_EOF_STR,      'E', NULL,                ARGS_REQUIRED },
    { OPT_EOF_OPT,      'e', "eof",               ARGS_OPTIONAL },
    { OPT_REPLACE_STR,  'I', NULL,                ARGS_REQUIRED },
    { OPT_REPLACE_OPT,  'i', "replace",           ARGS_OPTIONAL },
    { OPT_MAX_LINES,    'L', "max-lines",         ARGS_REQUIRED },
    { OPT_MAX_LINES_OPT,'l', NULL,                ARGS_OPTIONAL },
    { OPT_MAX_ARGS,     'n', "max-args",          ARGS_REQUIRED },
    { OPT_MAX_PROCS,    'P', "max-procs",         ARGS_REQUIRED },
    { OPT_INTERACTIVE,  'p', "interactive",       ARGS_NONE },
    { OPT_NO_RUN,       'r', "no-run-if-empty",   ARGS_NONE },
    { OPT_MAX_CHARS,    's', "max-chars",         ARGS_REQUIRED },
    { OPT_VERBOSE,      't', "verbose",           ARGS_NONE },
    { OPT_EXIT,         'x', "exit",              ARGS_NONE },
    { OPT_SHOW_LIMITS,  0,   "show-limits",       ARGS_NONE },
    { OPT_ACCEPTED,     0,   "open-tty",          ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 1, 0 };

typedef struct {
    int         nullSeparated;
    int         delimiter;          // -1: blanks and lines
    const char* argFile;
    const char* eofString;
    const char* replace;
    long        maxLines;
    long        maxArgs;
    long        maxProcs;
    int         interactive;
    int         noRunIfEmpty;
    long        maxChars;
    int         verbose;
    int         exitOnSize;
    int         showLimits;
} Config;

static const Config defaultConfig = { 0, -1, NULL, NULL, NULL, 0, 0, 1, 0, 0, DEFAULT_MAX_CHARS, 0, 0, 0 };

/** Everything one run keeps: the options, the command, the batch being filled and the children running. */
typedef struct {
    Config   config;
    int      status;
    HANDLE   running[MAXIMUM_WAIT_OBJECTS];
    int      runningCount;
    int      fatal;                 // a run ended with 255 or the command is not found: stop
    char**   commandWords;          // COMMAND and INITIAL-ARGS
    int      commandCount;
    size_t   baseLength;            // of them, with a separator each
    char**   batch;
    size_t   batchCount, batchCapacity, batchLength;
    long     linesInBatch;
    int      ranOnce;
} Xargs;

static int parse_count(const char* name, const char* text, long minimum, long* out) {
    char* end;
    long value = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || value < minimum) {
        fprintf(stderr, PROGRAM ": invalid number for %s option\n", name);
        return -1;
    }
    *out = value;
    return 0;
}

static int on_option(int id, const char* value, void* context) {
    Xargs* x = context;
    (void)context;
    switch (id) {
        case OPT_NULL:         x->config.nullSeparated = 1; x->config.delimiter = 0; break;
        case OPT_ARG_FILE:     x->config.argFile = value; break;
        case OPT_DELIMITER: {
            // a character, or one of the escapes \n \t \0 \\ or an octal/hex number
            if (value[0] == '\\' && value[1] != '\0') {
                switch (value[1]) {
                    case 'n': x->config.delimiter = '\n'; break;
                    case 't': x->config.delimiter = '\t'; break;
                    case '0': x->config.delimiter = 0; break;
                    case '\\': x->config.delimiter = '\\'; break;
                    case 'a': x->config.delimiter = '\a'; break;
                    case 'f': x->config.delimiter = '\f'; break;
                    case 'r': x->config.delimiter = '\r'; break;
                    case 'v': x->config.delimiter = '\v'; break;
                    case 'x': x->config.delimiter = (int)strtol(value + 2, NULL, 16) & 0xFF; break;
                    default:  x->config.delimiter = (int)strtol(value + 1, NULL, 8) & 0xFF; break;
                }
            } else if (value[0] != '\0' && value[1] == '\0') {
                x->config.delimiter = (unsigned char)value[0];
            } else {
                fprintf(stderr, PROGRAM ": invalid delimiter: %s\n", value);
                return -1;
            }
            break;
        }
        case OPT_EOF_STR:      x->config.eofString = value; break;
        case OPT_EOF_OPT:      x->config.eofString = value != NULL && value[0] != '\0' ? value : NULL; break;
        case OPT_REPLACE_STR:
        case OPT_REPLACE_OPT:
            if (x->config.maxArgs > 0 || x->config.maxLines > 0) {
                fprintf(stderr, PROGRAM ": warning: options --max-args and --replace are mutually exclusive, ignoring previous --max-args value\n");
                x->config.maxArgs = 0;
                x->config.maxLines = 0;
            }
            x->config.replace = id == OPT_REPLACE_STR || (value != NULL && value[0] != '\0') ? value : "{}";
            break;
        case OPT_MAX_LINES:
            if (x->config.replace) {
                fprintf(stderr, PROGRAM ": warning: options --max-lines and --replace are mutually exclusive, ignoring previous --replace value\n");
                x->config.replace = NULL;
            }
            return parse_count("-L", value, 1, &x->config.maxLines);
        case OPT_MAX_LINES_OPT:
            if (value == NULL || value[0] == '\0')
                x->config.maxLines = 1;
            else
                return parse_count("-l", value, 1, &x->config.maxLines);
            break;
        case OPT_MAX_ARGS:
            if (x->config.replace) {
                fprintf(stderr, PROGRAM ": warning: options --replace and --max-args/-n are mutually exclusive, ignoring previous --replace value\n");
                x->config.replace = NULL;
            }
            return parse_count("-n", value, 1, &x->config.maxArgs);
        case OPT_MAX_PROCS:    return parse_count("-P", value, 0, &x->config.maxProcs);
        case OPT_INTERACTIVE:  x->config.interactive = 1; x->config.verbose = 1; break;
        case OPT_NO_RUN:       x->config.noRunIfEmpty = 1; break;
        case OPT_MAX_CHARS:    return parse_count("-s", value, 1, &x->config.maxChars);
        case OPT_VERBOSE:      x->config.verbose = 1; break;
        case OPT_EXIT:         x->config.exitOnSize = 1; break;
        case OPT_SHOW_LIMITS:  x->config.showLimits = 1; break;
        default:               break;
    }
    return 0;
}

/* ---- reading items ------------------------------------------------------------------------- */

typedef struct {
    const unsigned char* data;
    size_t               length, pos;
} Input;

static int is_blank(int c) {
    return c == ' ' || c == '\t';
}

static char* make_item(const unsigned char* bytes, size_t length) {
    char* item = malloc(length + 1);
    if (item == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        exit(1);
    }
    memcpy(item, bytes, length);
    item[length] = '\0';
    return item;
}

/**
 * The next item. *endsLine is set when it is the last of its input line (for -L). Returns 1, 0 at the end
 * of the input, or -1 after a message (a quote that is not closed).
 */
static int next_item(Xargs* x, Input* in, char** out, int* endsLine) {
    {
        size_t capacity = 0, used = 0;
        unsigned char* text = NULL;
        int quote = 0, started = 0;

        *endsLine = 1;

        if (x->config.delimiter >= 0) {
            size_t start = in->pos;
            if (in->pos >= in->length)
                return 0;
            while (in->pos < in->length && in->data[in->pos] != (unsigned char)x->config.delimiter)
                in->pos++;
            *out = make_item(in->data + start, in->pos - start);
            if (in->pos < in->length)
                in->pos++;
            return 1;
        }

        // blanks (and, outside -I, line feeds) before the item
        for (;;) {
            if (in->pos >= in->length)
                return 0;
            if (is_blank(in->data[in->pos]) || in->data[in->pos] == '\n') {
                in->pos++;
                continue;
            }
            break;
        }

        for (;;) {
            int c;
            if (in->pos >= in->length) {
                if (quote) {
                    fprintf(stderr, PROGRAM ": unmatched %s quote; by default quotes are special to xargs unless you use the -0 option\n",
                            quote == '"' ? "double" : "single");
                    free(text);
                    return -1;
                }
                break;
            }
            c = in->data[in->pos++];

            if (quote) {
                if (c == '\n') {
                    fprintf(stderr, PROGRAM ": unmatched %s quote; by default quotes are special to xargs unless you use the -0 option\n",
                            quote == '"' ? "double" : "single");
                    free(text);
                    return -1;
                }
                if (c == quote) {
                    quote = 0;
                    continue;
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
                started = 1;
                continue;
            } else if (c == '\\' && in->pos < in->length && in->data[in->pos] != '\n') {
                c = in->data[in->pos++];
            } else if (c == '\n') {
                break;                          // the end of the item and of the line
            } else if (is_blank(c) && !x->config.replace) {
                // the item ends; the line goes on unless only blanks and a line feed follow
                size_t k = in->pos;
                while (k < in->length && is_blank(in->data[k]))
                    k++;
                if (k < in->length && in->data[k] == '\n')
                    in->pos = k + 1;            // trailing blanks: the line continues on the next one
                *endsLine = 0;
                break;
            }
            if (VEC_RESERVE(text, capacity, used + 2) != 0) {
                fprintf(stderr, PROGRAM ": memory exhausted\n");
                exit(1);
            }
            text[used++] = (unsigned char)c;
        }
        (void)started;

        *out = make_item(text != NULL ? text : (const unsigned char*)"", used);
        free(text);
        return 1;
    }
}

/* ---- running the command ------------------------------------------------------------------- */

static void note_exit(Xargs* x, DWORD code) {
    if (code == 255) {
        fprintf(stderr, PROGRAM ": the command exited with status 255; aborting\n");
        x->status = 124;
        x->fatal = 1;
    } else if (code != 0 && x->status != 124) {
        x->status = 123;
    }
}

static void wait_one(Xargs* x) {
    DWORD which, code = 1;
    if (x->runningCount == 0)
        return;
    which = WaitForMultipleObjects((DWORD)x->runningCount, x->running, FALSE, INFINITE) - WAIT_OBJECT_0;
    GetExitCodeProcess(x->running[which], &code);
    CloseHandle(x->running[which]);
    x->running[which] = x->running[--x->runningCount];
    note_exit(x, code);
}

static void wait_all(Xargs* x) {
    while (x->runningCount > 0)
        wait_one(x);
}

static int confirm_run(char* const* argv) {
    FILE* tty = fopen("CONIN$", "r");
    char answer[16];
    size_t i;

    for (i = 0; argv[i] != NULL; i++)
        fprintf(stderr, "%s%s", i > 0 ? " " : "", argv[i]);
    fprintf(stderr, " ?...");
    fflush(stderr);
    if (tty == NULL || fgets(answer, sizeof answer, tty) == NULL) {
        if (tty != NULL)
            fclose(tty);
        return 0;
    }
    fclose(tty);
    return answer[0] == 'y' || answer[0] == 'Y';
}

static void run_command(Xargs* x, char** argv) {
    char path[VPATH_MAX];
    char* command;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES inherit = { sizeof inherit, NULL, TRUE };
    HANDLE in;
    size_t i;

    if (x->config.verbose && !x->config.interactive) {
        for (i = 0; argv[i] != NULL; i++)
            fprintf(stderr, "%s%s", i > 0 ? " " : "", argv[i]);
        fputc('\n', stderr);
    }
    if (x->config.interactive && !confirm_run(argv))
        return;

    if (program_find(argv[0], path, sizeof path) != 0 &&
        SearchPathA(NULL, argv[0], ".exe", sizeof path, path, NULL) == 0) {
        fprintf(stderr, PROGRAM ": %s: No such file or directory\n", argv[0]);
        x->status = 127;
        x->fatal = 1;
        return;
    }
    command = cmdline_build(argv);
    if (command == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        x->status = 1;
        x->fatal = 1;
        return;
    }
    if (strlen(command) > 32000) {
        fprintf(stderr, PROGRAM ": argument list too long\n");
        free(command);
        x->status = 1;
        x->fatal = 1;
        return;
    }

    while (x->runningCount >= x->config.maxProcs && x->config.maxProcs > 0)
        wait_one(x);

    in = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, NULL);
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    SetHandleInformation(si.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(si.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    fflush(stdout);
    if (!CreateProcessA(path, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, PROGRAM ": cannot run %s\n", argv[0]);
        x->status = 126;
        x->fatal = 1;
    } else {
        CloseHandle(pi.hThread);
        if (x->runningCount < MAXIMUM_WAIT_OBJECTS)
            x->running[x->runningCount++] = pi.hProcess;
        else
            CloseHandle(pi.hProcess);
        if (x->config.maxProcs == 1)
            wait_one(x);
    }
    CloseHandle(in);
    free(command);
}

/* ---- batches ------------------------------------------------------------------------------- */

static void run_batch(Xargs* x) {
    char** argv;
    int i;
    size_t k;

    argv = calloc((size_t)x->commandCount + x->batchCount + 1, sizeof *argv);
    if (argv == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        exit(1);
    }
    for (i = 0; i < x->commandCount; i++)
        argv[i] = x->commandWords[i];
    for (k = 0; k < x->batchCount; k++)
        argv[x->commandCount + k] = x->batch[k];
    run_command(x, argv);
    x->ranOnce = 1;
    free(argv);
    for (k = 0; k < x->batchCount; k++)
        free(x->batch[k]);
    x->batchCount = 0;
    x->batchLength = x->baseLength;
    x->linesInBatch = 0;
}

/** -I: the command with the replacement string replaced by the item, run once. */
static void run_replace(Xargs* x, const char* item) {
    char** argv = calloc((size_t)x->commandCount + 1, sizeof *argv);
    size_t replaceLength = strlen(x->config.replace), total = 0;
    int i;

    if (argv == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        exit(1);
    }
    for (i = 0; i < x->commandCount; i++) {
        const char* word = x->commandWords[i];
        size_t itemLength = strlen(item), count = 0, length = strlen(word);
        const char* p;
        char* out;
        size_t o = 0;

        for (p = word; (p = strstr(p, x->config.replace)) != NULL; p += replaceLength)
            count++;
        out = malloc(length + count * itemLength + 1);
        if (out == NULL)
            exit(1);
        for (p = word; *p != '\0';) {
            if (strncmp(p, x->config.replace, replaceLength) == 0) {
                memcpy(out + o, item, itemLength);
                o += itemLength;
                p += replaceLength;
            } else {
                out[o++] = *p++;
            }
        }
        out[o] = '\0';
        argv[i] = out;
        total += o + 1;
    }
    if (total > (size_t)x->config.maxChars) {
        fprintf(stderr, PROGRAM ": argument line too long\n");
        x->status = 1;
        x->fatal = 1;
    } else {
        run_command(x, argv);
        x->ranOnce = 1;
    }
    for (i = 0; i < x->commandCount; i++)
        free(argv[i]);
    free(argv);
}

static void add_item(Xargs* x, char* item, int endsLine) {
    size_t length = strlen(item) + 1;

    if (x->batchCount > 0 && x->batchLength + length > (size_t)x->config.maxChars) {
        if (x->config.exitOnSize && (x->config.maxArgs == 0 || x->batchCount < (size_t)x->config.maxArgs)) {
            fprintf(stderr, PROGRAM ": argument line too long\n");
            x->status = 1;
            x->fatal = 1;
            free(item);
            return;
        }
        run_batch(x);
        if (x->fatal) {
            free(item);
            return;
        }
    }
    if (x->batchCount == 0 && x->baseLength + length > (size_t)x->config.maxChars) {
        fprintf(stderr, PROGRAM ": can't fit single argument within argument list size limit\n");
        x->status = 1;
        x->fatal = 1;
        free(item);
        return;
    }
    if (VEC_RESERVE(x->batch, x->batchCapacity, x->batchCount + 1) != 0) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        exit(1);
    }
    x->batch[x->batchCount++] = item;
    x->batchLength += length;

    if (endsLine)
        x->linesInBatch++;
    if ((x->config.maxArgs > 0 && x->batchCount >= (size_t)x->config.maxArgs) ||
        (x->config.maxLines > 0 && x->linesInBatch >= x->config.maxLines))
        run_batch(x);
}

static void show_limits(Xargs* x) {
    fprintf(stderr, "POSIX upper limit on argument length (this system): 32767\n"
                    "POSIX smallest allowable upper limit on argument length (all systems): 4096\n"
                    "Maximum length of command we could actually use: %d\n"
                    "Size of command buffer we are actually using: %ld\n"
                    "Maximum parallelism (--max-procs must be no greater): 64\n\n",
            DEFAULT_MAX_CHARS, x->config.maxChars);
}

/** Reads the items of the input one by one and hands them to the batches (or runs the -I command). */
static void process_items(Xargs* x, Input* input) {
    while (!x->fatal) {
        char* item;
        int endsLine;
        int r = next_item(x, input, &item, &endsLine);

        if (r < 0) {
            x->status = 1;
            break;
        }
        if (r == 0)
            break;
        if (x->config.eofString != NULL && x->config.delimiter < 0 && strcmp(item, x->config.eofString) == 0) {
            free(item);
            break;
        }
        if (x->config.replace) {
            run_replace(x, item);
            free(item);
        } else {
            add_item(x, item, endsLine);
        }
    }
}

int main(int argc, char* argv[]) {
    const char** operands;
    int operandCount, result, i;
    FILE* source = stdin;
    unsigned char* data;
    size_t length;
    Input input;
    static char* defaultCommand[] = { "echo", NULL };
    Xargs state;
    Xargs* x = &state;

    memset(x, 0, sizeof *x);
    x->config = defaultConfig;

    result = args_parse(&spec, argc, argv, on_option, x, &operands, &operandCount);
    if (result != ARGS_OK)
        return result == ARGS_EXIT ? 0 : 1;

    if (x->config.maxProcs == 0)
        x->config.maxProcs = MAXIMUM_WAIT_OBJECTS - 1;
    if (x->config.maxProcs > MAXIMUM_WAIT_OBJECTS - 1)
        x->config.maxProcs = MAXIMUM_WAIT_OBJECTS - 1;
    if (x->config.showLimits)
        show_limits(x);

    if (operandCount == 0) {
        x->commandWords = defaultCommand;
        x->commandCount = 1;
    } else {
        x->commandWords = (char**)operands;
        x->commandCount = operandCount;
    }
    for (i = 0; i < x->commandCount; i++)
        x->baseLength += strlen(x->commandWords[i]) + 1;
    x->batchLength = x->baseLength;

    text_binary_streams();
    if (x->config.argFile != NULL && text_open(PROGRAM, x->config.argFile, 1, &source) != 0)
        return 1;
    data = text_slurp(source, &length);
    if (data == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return 1;
    }
    input.data = data;
    input.length = length;
    input.pos = 0;

    process_items(x, &input);

    if (!x->fatal && result == ARGS_OK) {
        if (x->batchCount > 0)
            run_batch(x);
        else if (!x->ranOnce && !x->config.noRunIfEmpty && !x->config.replace)
            run_batch(x);
    }
    wait_all(x);
    fflush(stdout);
    return x->status;
}
