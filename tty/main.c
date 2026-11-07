/*
 * tty: prints the terminal that is the standard input. There are no tty devices on Windows: a console is
 * called /dev/console. The exit status is 0 for a terminal, 1 when it is not one, 2 for a bad option.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "tty"

static void print_usage(void) {
    puts("Usage: tty [OPTION]...\n"
         "Print the file name of the terminal connected to standard input.\n"
         "\n"
         "  -s, --silent, --quiet   print nothing, only return an exit status\n"
         "      --help              display this help and exit\n"
         "      --version           output version information and exit");
}

enum { OPT_SILENT = 1 };

static const ArgsOption options[] = {
    { OPT_SILENT, 's', "silent", ARGS_NONE },
    { OPT_SILENT, 0,   "quiet",  ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 2, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    (void)id;
    (void)value;
    *(int*)context = 1;
    return 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, silent = 0;
    DWORD mode;
    _setmode(_fileno(stdout), _O_BINARY);   // a line ends with LF, as the other tools write it

    result = args_parse(&spec, argc, argv, on_option, &silent, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 0) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[0]);
        args_usage_error(&spec);
        return 2;
    }
    if (!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode)) {
        if (!silent)
            puts("not a tty");
        return 1;
    }
    if (!silent)
        puts("/dev/console");
    return 0;
}
