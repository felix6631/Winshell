#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "printenv"

static void print_usage(void) {
    puts("Usage: printenv [OPTION]... [NAME]...\n"
         "Print the values of the specified environment variables, or all of them\n"
         "as NAME=value when there is no NAME.\n"
         "\n"
         "  -0, --null     end each output line with NUL, not newline\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Exit status: 0, 1 when a NAME is not in the environment, 2 for a bad option.");
}

enum { OPT_NULL = 1 };

static const ArgsOption options[] = {
    { OPT_NULL, '0', "null", ARGS_NONE },
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
    int nullEnd = 0, i = 0, status = 0, count, result;
    const char** operands;
    char terminator;

    result = args_parse(&spec, argc, argv, on_option, &nullEnd, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    terminator = nullEnd ? '\0' : '\n';
    _setmode(_fileno(stdout), _O_BINARY);

    if (i >= count) {
        char* strings = GetEnvironmentStringsA();
        const char* p;
        if (strings == NULL)
            return 2;
        for (p = strings; *p != '\0'; p += strlen(p) + 1) {
            if (*p == '=')              // the per-drive directories of Windows are not variables
                continue;
            fputs(p, stdout);
            putchar(terminator);
        }
        FreeEnvironmentStringsA(strings);
        return ferror(stdout) ? 2 : 0;
    }

    for (; i < count; i++) {
        DWORD length;
        char* value;

        if (strchr(operands[i], '=') != NULL) {     // a name never has one
            status = 1;
            continue;
        }
        length = GetEnvironmentVariableA(operands[i], NULL, 0);
        if (length == 0) {
            status = 1;
            continue;
        }
        value = malloc(length);
        if (value == NULL)
            return 2;
        GetEnvironmentVariableA(operands[i], value, length);
        fputs(value, stdout);
        putchar(terminator);
        free(value);
    }
    return status;
}
