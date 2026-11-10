/*
 * users: prints the names of the users logged in, sorted, on a line separated by blanks, as GNU users does. Windows has
 * no utmp file: the users are the ones of the open shells of Winshell (the sessions that who lists; see
 * common/sessions.h), a user once for every shell. A FILE operand is accepted and not read.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "sessions.h"

#define VERSION "0.1"
#define PROGRAM "users"

static void print_usage(void) {
    puts("Usage: users [OPTION]... [FILE]\n"
         "Output who is currently logged in according to FILE.\n"
         "If FILE is not specified, use the sessions of the shells of Winshell (there is no /var/run/utmp).\n"
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

static int compare(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b);
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, n, i;
    Session* sessions = NULL;
    char (*names)[256];

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 1) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[1]);
        args_usage_error(&spec);
        return 1;
    }
    n = session_list(&sessions);
    if (n <= 0) {
        free(sessions);
        return 0;
    }
    names = (char(*)[256])malloc((size_t)n * sizeof *names);
    if (names == NULL) {
        free(sessions);
        return 1;
    }
    for (i = 0; i < n; i++)
        snprintf(names[i], sizeof names[i], "%s", sessions[i].user);
    qsort(names, (size_t)n, sizeof names[0], compare);
    for (i = 0; i < n; i++) {
        if (i > 0)
            putchar(' ');
        fputs(names[i], stdout);
    }
    putchar('\n');
    free(names);
    free(sessions);
    return 0;
}
