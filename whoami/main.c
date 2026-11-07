/*
 * whoami: prints the name of the user the shell runs as.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "whoami"

static void print_usage(void) {
    puts("Usage: whoami [OPTION]...\n"
         "Print the user name associated with the current effective user ID.\n"
         "Same as id -un.\n"
         "\n"
         "      --help        display this help and exit\n"
         "      --version     output version information and exit");
}

static const ArgsOption options[] = { { 0 } };
static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    (void)id;
    (void)value;
    (void)context;
    return 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result;
    char name[256];
    DWORD size = sizeof name;
    _setmode(_fileno(stdout), _O_BINARY);   // a line ends with LF, as the other tools write it

    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 0) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[0]);
        args_usage_error(&spec);
        return 1;
    }
    if (!GetUserNameA(name, &size)) {
        fprintf(stderr, PROGRAM ": cannot find name for user ID\n");
        return 1;
    }
    printf("%s\n", name);
    return 0;
}
