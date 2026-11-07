/*
 * link: makes a hard link to a file with the system call that does only that.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "link"

static void print_usage(void) {
    puts("Usage: link FILE1 FILE2\n"
         "  or:  link OPTION\n"
         "Call the link function to create a link named FILE2 to an existing FILE1.\n"
         "\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit");
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
    char first[VPATH_MAX], second[VPATH_MAX];

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count < 2) {
        if (count == 1)
            fprintf(stderr, PROGRAM ": missing operand after '%s'\n", operands[0]);
        else
            fprintf(stderr, PROGRAM ": missing operand\n");
        args_usage_error(&spec);
        return 1;
    }
    if (count > 2) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[2]);
        args_usage_error(&spec);
        return 1;
    }
    if (vpath_to_native(operands[0], first, sizeof first) != 0 || vpath_to_native(operands[1], second, sizeof second) != 0) {
        fprintf(stderr, PROGRAM ": cannot create link '%s' to '%s': Invalid argument\n", operands[1], operands[0]);
        return 1;
    }
    if (!CreateHardLinkA(second, first, NULL)) {
        fprintf(stderr, PROGRAM ": cannot create link '%s' to '%s': %s\n", operands[1], operands[0],
                fs_strerror(GetLastError()));
        return 1;
    }
    return 0;
}
