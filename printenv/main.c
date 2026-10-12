#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define VERSION "0.1"

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

int main(int argc, char* argv[]) {
    int nullEnd = 0, i = 1, status = 0, options = 1;
    char terminator;

    for (; i < argc && options; i++) {
        const char* arg = argv[i];
        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }
        if (arg[0] != '-' || arg[1] == '\0') {
            break;
        } else if (strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(arg, "--version") == 0) {
            puts("printenv (Winshell) " VERSION);
            return 0;
        } else if (strcmp(arg, "--null") == 0) {
            nullEnd = 1;
        } else if (arg[1] != '-' && strspn(arg + 1, "0") == strlen(arg + 1)) {
            nullEnd = 1;
        } else {
            fprintf(stderr, "printenv: invalid option -- '%s'\nTry 'printenv --help' for more information.\n", arg);
            return 2;
        }
    }

    terminator = nullEnd ? '\0' : '\n';
    _setmode(_fileno(stdout), _O_BINARY);

    if (i >= argc) {
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

    for (; i < argc; i++) {
        DWORD length;
        char* value;

        if (strchr(argv[i], '=') != NULL) {     // a name never has one
            status = 1;
            continue;
        }
        length = GetEnvironmentVariableA(argv[i], NULL, 0);
        if (length == 0) {
            status = 1;
            continue;
        }
        value = malloc(length);
        if (value == NULL)
            return 2;
        GetEnvironmentVariableA(argv[i], value, length);
        fputs(value, stdout);
        putchar(terminator);
        free(value);
    }
    return status;
}
