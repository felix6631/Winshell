/*
 * nohup: runs a command that keeps going when the terminal goes away, as GNU nohup does: if standard input is a
 * terminal it is taken from an unreadable file (NUL), if standard output is a terminal it is appended to nohup.out
 * (or to nohup.out in the home directory), and standard error that is a terminal goes to standard output. Windows has
 * no SIGHUP to ignore, so that part is left out. Status: 125 for nohup itself failing, 126 and 127 as the shell has them.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "exec.h"
#include "fsutil.h"

#define VERSION "0.1"
#define PROGRAM "nohup"

static void print_usage(void) {
    puts("Usage: nohup COMMAND [ARG]...\n"
         "  or:  nohup OPTION\n"
         "Run COMMAND, ignoring hangup signals.\n"
         "\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "If standard input is a terminal, redirect it from an unreadable file.\n"
         "If standard output is a terminal, append output to 'nohup.out' if possible,\n"
         "'$HOME/nohup.out' otherwise.\n"
         "If standard error is a terminal, redirect it to standard output.\n"
         "To save output to FILE, use 'nohup COMMAND > FILE'.\n"
         "\n"
         "NOTE: your shell may have its own version of nohup, which usually supersedes\n"
         "the version described here.  Please refer to your shell's documentation\n"
         "for details about the options it supports.");
}

static const ArgsOption options[] = { { 0 } };
static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 125, 1, 0 };

static int on_option(int id, const char* value, void* context) {
    (void)id;
    (void)value;
    (void)context;
    return 0;
}

static int is_terminal(HANDLE h) {
    DWORD mode;

    return h != INVALID_HANDLE_VALUE && h != NULL && GetFileType(h) == FILE_TYPE_CHAR && GetConsoleMode(h, &mode);
}

static HANDLE open_append(const char* path) {
    SECURITY_ATTRIBUTES sa;

    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    return CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE), err = GetStdHandle(STD_ERROR_HANDLE);
    int ignoreInput, redirectOut, redirectErr;
    char outName[MAX_PATH * 2] = "nohup.out";
    const char* message = NULL;

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        args_usage_error(&spec);
        return 125;
    }
    ignoreInput = is_terminal(in);
    redirectOut = is_terminal(out);
    redirectErr = is_terminal(err);
    if (ignoreInput) {
        SECURITY_ATTRIBUTES sa;

        sa.nLength = sizeof sa;
        sa.lpSecurityDescriptor = NULL;
        sa.bInheritHandle = TRUE;
        in = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (in == INVALID_HANDLE_VALUE) {
            fprintf(stderr, PROGRAM ": failed to open NUL\n");
            return 125;
        }
    }
    if (redirectOut) {
        out = open_append(outName);
        if (out == INVALID_HANDLE_VALUE) {
            const char* home = getenv("USERPROFILE");

            if (home == NULL)
                home = getenv("HOME");
            if (home != NULL) {
                snprintf(outName, sizeof outName, "%s\\nohup.out", home);
                out = open_append(outName);
            }
        }
        if (out == INVALID_HANDLE_VALUE) {
            fprintf(stderr, PROGRAM ": failed to open 'nohup.out'\n");
            return 125;
        }
    }
    if (redirectErr)
        err = out;
    if (ignoreInput && redirectOut)
        message = "ignoring input and appending output to '%s'";
    else if (ignoreInput && redirectErr)
        message = "ignoring input and redirecting stderr to stdout";
    else if (ignoreInput)
        message = "ignoring input";
    else if (redirectOut)
        message = "appending output to '%s'";
    else if (redirectErr)
        message = "redirecting stderr to stdout";
    if (message != NULL) {
        char text[MAX_PATH * 2 + 64];

        snprintf(text, sizeof text, message, outName);
        // the message goes to the original standard error unless that was the terminal and is now the file
        fprintf(stderr, PROGRAM ": %s\n", text);
    }
    fflush(stderr);
    return exec_run(PROGRAM, 1, (char* const*)operands, 0, in, out, err);
}
