#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "dirname"

static void print_usage(void) {
    puts("Usage: dirname [OPTION] NAME...\n"
         "Output each NAME with its last non-slash component and trailing slashes\n"
         "removed; if NAME contains no /'s, output '.' (meaning the current directory).\n"
         "\n"
         "  -z, --zero     end each output line with NUL, not newline\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Both / and \\ separate the components of a name.\n"
         "\n"
         "Examples:\n"
         "  dirname /usr/bin/          -> \"/usr\"\n"
         "  dirname dir1/str dir2/str  -> \"dir1\" followed by \"dir2\"\n"
         "  dirname stdio.h            -> \".\"");
}

enum { OPT_ZERO = 1 };

static const ArgsOption options[] = {
    { OPT_ZERO, 'z', "zero", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    (void)value;
    if (id == OPT_ZERO)
        *(int*)context = 1;
    return 0;
}

static int is_separator(char c) {
    return c == '/' || c == '\\';
}

/** The directory part of name: everything before the last component, without the separators in front of it. */
static void write_dirname(const char* name, int terminator) {
    size_t length = strlen(name), i;

    while (length > 1 && is_separator(name[length - 1]))
        length--;

    i = length;
    while (i > 0 && !is_separator(name[i - 1]))
        i--;

    if (i == 0) {
        fputs(".", stdout);             // no directory part
    } else {
        while (i > 1 && is_separator(name[i - 1]))
            i--;
        fwrite(name, 1, i, stdout);
    }
    putchar(terminator);
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, i, zero = 0;

    result = args_parse(&spec, argc, argv, on_option, &zero, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing operand\nTry '" PROGRAM " --help' for more information.\n");
        return 1;
    }

    _setmode(_fileno(stdout), _O_BINARY);
    for (i = 0; i < count; i++)
        write_dirname(operands[i], zero ? '\0' : '\n');
    return fflush(stdout) != 0 ? 1 : 0;
}
