/*
 * yes: prints its arguments, joined with spaces, again and again (y when there are none), until the reader
 * of its output has had enough.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "yes"

static void print_usage(void) {
    puts("Usage: yes [STRING]...\n"
         "  or:  yes OPTION\n"
         "Repeatedly output a line with all specified STRING(s), or 'y'.\n"
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
    int count, result, i;
    size_t lineLength = 0, used = 0, size;
    char* line;
    char* buffer;

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    if (count == 0) {
        line = _strdup("y\n");
        lineLength = 2;
    } else {
        for (i = 0; i < count; i++)
            lineLength += strlen(operands[i]) + 1;
        line = malloc(lineLength + 1);
        if (line != NULL) {
            for (i = 0; i < count; i++) {
                size_t n = strlen(operands[i]);

                memcpy(line + used, operands[i], n);
                used += n;
                line[used++] = i + 1 < count ? ' ' : '\n';
            }
        }
    }
    if (line == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return 1;
    }

    // many lines to the write, as many as fit in 64 KiB (a longer line goes alone)
    size = lineLength < 65536 ? (65536 / lineLength) * lineLength : lineLength;
    buffer = malloc(size);
    if (buffer == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return 1;
    }
    for (used = 0; used < size; used += lineLength)
        memcpy(buffer + used, line, lineLength);

    for (;;) {
        if (fwrite(buffer, 1, size, stdout) != size || fflush(stdout) != 0) {
            // the reader has gone: the pipe is closed
            fprintf(stderr, PROGRAM ": standard output: Broken pipe\n");
            return 1;
        }
    }
}
