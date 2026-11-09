/*
 * users: prints the names of the users logged in, sorted, on a line separated by blanks, as GNU users does. Windows
 * has no utmp file: the sessions are asked of the system (the active and the connected ones). A FILE operand is
 * accepted and ignored, as the sessions are not kept in a file.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wtsapi32.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "users"

static void print_usage(void) {
    puts("Usage: users [OPTION]... [FILE]\n"
         "Output who is currently logged in according to FILE.\n"
         "If FILE is not specified, use the sessions of the system (there is no /var/run/utmp).\n"
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
    return strcmp(*(char* const*)a, *(char* const*)b);
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, n = 0, i;
    WTS_SESSION_INFOA* sessions = NULL;
    DWORD sessionCount = 0, s;
    char* names[256];

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 1) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[1]);
        args_usage_error(&spec);
        return 1;
    }
    if (WTSEnumerateSessionsA(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &sessionCount)) {
        for (s = 0; s < sessionCount && n < 256; s++) {
            LPSTR user = NULL;
            DWORD bytes = 0;

            if (sessions[s].State != WTSActive && sessions[s].State != WTSConnected)
                continue;
            if (WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE, sessions[s].SessionId, WTSUserName, &user, &bytes)) {
                if (user != NULL && user[0] != '\0')
                    names[n++] = _strdup(user);
                WTSFreeMemory(user);
            }
        }
        WTSFreeMemory(sessions);
    }
    qsort(names, (size_t)n, sizeof names[0], compare);
    for (i = 0; i < n; i++) {
        if (i > 0)
            putchar(' ');
        fputs(names[i], stdout);
        free(names[i]);
    }
    if (n > 0)
        putchar('\n');
    return 0;
}
