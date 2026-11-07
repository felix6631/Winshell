/*
 * unlink: removes one file with the system call that does only that (not a directory).
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
#define PROGRAM "unlink"

static void print_usage(void) {
    puts("Usage: unlink FILE\n"
         "  or:  unlink OPTION\n"
         "Call the unlink function to remove the specified FILE.\n"
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
    char native[VPATH_MAX];
    DWORD attributes;

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        args_usage_error(&spec);
        return 1;
    }
    if (count > 1) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[1]);
        args_usage_error(&spec);
        return 1;
    }
    if (vpath_to_native(operands[0], native, sizeof native) != 0) {
        fprintf(stderr, PROGRAM ": cannot unlink '%s': Is a directory\n", operands[0]);
        return 1;
    }
    if (fs_attributes(native, &attributes) != 0) {
        fprintf(stderr, PROGRAM ": cannot unlink '%s': %s\n", operands[0], fs_strerror(GetLastError()));
        return 1;
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        // a link to a directory (a junction) is removed by itself; a directory is not removed
        if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            fprintf(stderr, PROGRAM ": cannot unlink '%s': Is a directory\n", operands[0]);
            return 1;
        }
        if (!RemoveDirectoryA(native)) {
            fprintf(stderr, PROGRAM ": cannot unlink '%s': %s\n", operands[0], fs_strerror(GetLastError()));
            return 1;
        }
        return 0;
    }
    if (attributes & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesA(native, attributes & ~FILE_ATTRIBUTE_READONLY);    // unlinking does not depend on the permissions
    if (!DeleteFileA(native)) {
        fprintf(stderr, PROGRAM ": cannot unlink '%s': %s\n", operands[0], fs_strerror(GetLastError()));
        return 1;
    }
    return 0;
}
