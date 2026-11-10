/*
 * uptime: tells how long the system has been running, as GNU uptime does: the time of day, how long the system has been up
 * (since the boot, from the time Windows has been running), the number of users (the open shells of Winshell; see
 * common/sessions.h) and the load average. Windows has no load average: it is shown as 0.00, 0.00, 0.00. A FILE operand
 * is accepted and not read.
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
#define PROGRAM "uptime"

static void print_usage(void) {
    puts("Usage: uptime [OPTION]... [FILE]\n"
         "Print the current time, the length of time the system has been up,\n"
         "the number of users on the system, and the average number of jobs\n"
         "in the run queue over the last 1, 5 and 15 minutes (Windows has no load average: 0.00 is shown).\n"
         "The sessions are the open shells of Winshell (there is no /var/run/utmp).\n"
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
    int count, result, users;
    Session* sessions = NULL;
    SYSTEMTIME now;
    unsigned long long seconds;
    long updays, uphours, upmins;

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 1) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[1]);
        args_usage_error(&spec);
        return 1;
    }
    users = session_list(&sessions);
    free(sessions);
    if (users < 0)
        users = 0;

    GetLocalTime(&now);
    seconds = GetTickCount64() / 1000ULL;
    updays = (long)(seconds / 86400ULL);
    uphours = (long)((seconds - (unsigned long long)updays * 86400ULL) / 3600ULL);
    upmins = (long)((seconds - (unsigned long long)updays * 86400ULL) / 60ULL) % 60;

    printf(" %02d:%02d:%02d ", now.wHour, now.wMinute, now.wSecond);
    if (updays > 0)
        printf("up %ld day%s %2ld:%02ld,  ", updays, updays == 1 ? "" : "s", uphours, upmins);
    else
        printf("up  %2ld:%02ld,  ", uphours, upmins);
    printf("%d user%s", users, users == 1 ? "" : "s");
    printf(",  load average: %.2f, %.2f, %.2f\n", 0.0, 0.0, 0.0);
    fflush(stdout);
    return 0;
}
