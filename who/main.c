/*
 * who: shows who is logged in, as GNU who does. Windows has no utmp file: the terminals are the sessions that the shells
 * of Winshell record when they start (see common/sessions.h), the time of a session is the time its shell was opened, and
 * the line is pts/N. The system boot time is the time the system has been running subtracted from now. Dead processes,
 * logins, init spawns, run levels and clock changes do not exist here: -d -l -p -r -t print nothing for them.
 * A FILE operand names a utmp file; the sessions are not kept in a file, so it is accepted and not read.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "sessions.h"

#define VERSION "0.1"
#define PROGRAM "who"

static void print_usage(void) {
    puts("Usage: who [OPTION]... [ FILE | ARG1 ARG2 ]\n"
         "Print information about users who are currently logged in.\n"
         "\n"
         "  -a, --all         same as -b -d --login -p -r -t -T -u\n"
         "  -b, --boot        time of last system boot\n"
         "  -d, --dead        print dead processes\n"
         "  -H, --heading     print line of column headings\n"
         "  -l, --login       print system login processes\n"
         "      --lookup      attempt to canonicalize hostnames via DNS\n"
         "  -m                only hostname and user associated with stdin\n"
         "  -p, --process     print active processes spawned by init\n"
         "  -q, --count       all login names and number of users logged on\n"
         "  -r, --runlevel    print current runlevel\n"
         "  -s, --short       print only name, line, and time (default)\n"
         "  -t, --time        print last system clock change\n"
         "  -T, -w, --mesg    add user's message status as +, - or ?\n"
         "  -u, --users       list users logged in\n"
         "      --message     same as -T\n"
         "      --writable    same as -T\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "If FILE is not specified, use the sessions of the shells of Winshell (there is no /var/run/utmp).\n"
         "If ARG1 ARG2 given, -m presumed: 'am i' or 'mom likes' are usual.");
}

enum { OPT_LOOKUP = 256 };

static const ArgsOption options[] = {
    { 'a', 'a', "all",      ARGS_NONE },
    { 'b', 'b', "boot",     ARGS_NONE },
    { 'd', 'd', "dead",     ARGS_NONE },
    { 'H', 'H', "heading",  ARGS_NONE },
    { 'l', 'l', "login",    ARGS_NONE },
    { OPT_LOOKUP, 0, "lookup", ARGS_NONE },
    { 'm', 'm', NULL,       ARGS_NONE },
    { 'p', 'p', "process",  ARGS_NONE },
    { 'q', 'q', "count",    ARGS_NONE },
    { 'r', 'r', "runlevel", ARGS_NONE },
    { 's', 's', "short",    ARGS_NONE },
    { 't', 't', "time",     ARGS_NONE },
    { 'T', 'T', "mesg",     ARGS_NONE },
    { 'T', 'w', NULL,       ARGS_NONE },
    { 'T', 0,   "message",  ARGS_NONE },
    { 'T', 0,   "writable", ARGS_NONE },
    { 'u', 'u', "users",    ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int needUsers, needBoot, needDead, needLogin, needProcess, needRunlevel, needClock, needCount;
    int heading, includeMesg, includeIdle, includeExit, shortOutput, mineOnly;
} Who;

static int on_option(int id, const char* value, void* context) {
    Who* w = (Who*)context;

    (void)value;
    switch (id) {
    case 'a':
        w->needBoot = w->needDead = w->needLogin = w->needProcess = w->needRunlevel = w->needClock = w->needUsers = 1;
        w->includeMesg = w->includeIdle = w->includeExit = 1;
        w->shortOutput = 0;
        break;
    case 'b': w->needBoot = 1; w->shortOutput = 0; break;
    case 'd': w->needDead = 1; w->includeIdle = 1; w->includeExit = 1; w->shortOutput = 0; break;
    case 'H': w->heading = 1; break;
    case 'l': w->needLogin = 1; w->includeIdle = 1; w->shortOutput = 0; break;
    case 'm': w->mineOnly = 1; break;
    case 'p': w->needProcess = 1; w->shortOutput = 0; break;
    case 'q': w->needCount = 1; break;
    case 'r': w->needRunlevel = 1; w->includeIdle = 1; w->shortOutput = 0; break;
    case 's': w->shortOutput = 1; break;
    case 't': w->needClock = 1; w->shortOutput = 0; break;
    case 'T': w->includeMesg = 1; break;
    case 'u': w->needUsers = 1; w->includeIdle = 1; w->shortOutput = 0; break;
    default: break;
    }
    return 0;
}

/** One line, as GNU who's print_line does it: the fields in their columns, the trailing blanks cut off. */
static void print_line(const Who* w, const char* user, char state, const char* line, const char* time, const char* idle,
                       const char* pid, const char* comment) {
    char buffer[1024];
    char idleText[32] = "", pidText[32] = "", mesg[3] = { ' ', 'x', '\0' };
    size_t n;

    mesg[1] = state;
    if (w->includeIdle && !w->shortOutput)
        snprintf(idleText, sizeof idleText, " %-6s", idle);
    if (!w->shortOutput)
        snprintf(pidText, sizeof pidText, " %10s", pid);
    snprintf(buffer, sizeof buffer, "%-8s%s %-12s %-12s%s%s %-8s", user, w->includeMesg ? mesg : "", line, time, idleText,
             pidText, comment);
    n = strlen(buffer);
    while (n > 0 && buffer[n - 1] == ' ')
        buffer[--n] = '\0';
    fputs(buffer, stdout);
    putchar('\n');
}

static void print_heading(const Who* w) {
    printf("%-8s ", "NAME");
    if (w->includeMesg)
        printf("  ");
    printf("%-12s ", "LINE");
    printf("%-12s ", "TIME");
    if (w->includeIdle)
        printf("%-6s ", "IDLE");
    if (w->includeIdle || !w->shortOutput)
        printf("%10s ", "PID");
    printf("%s", "COMMENT");
    if (w->includeExit)
        printf("  %s", "EXIT");
    putchar('\n');
}

int main(int argc, char* argv[]) {
    Who w;
    const char** operands;
    int count, result, n, i;
    Session* sessions = NULL;
    const char* mine = NULL;

    memset(&w, 0, sizeof w);
    w.shortOutput = 1;
    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, &w, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    if (count == 2) {
        w.mineOnly = 1;                                 /* who am i */
    } else if (count > 2) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[2]);
        args_usage_error(&spec);
        return 1;
    }
    /* a single operand is a utmp file: the sessions are not kept in a file, so it is not read */
    if (!(w.needBoot || w.needDead || w.needLogin || w.needProcess || w.needRunlevel || w.needClock || w.needUsers))
        w.needUsers = 1;
    if (w.mineOnly) {
        w.needUsers = 1;
        mine = session_current_tty();
    }

    n = session_list(&sessions);
    if (n < 0)
        n = 0;

    if (w.needCount) {
        for (i = 0; i < n; i++) {
            if (mine != NULL && strcmp(sessions[i].tty, mine) != 0)
                continue;
            printf("%s%s", i > 0 ? " " : "", sessions[i].user);
        }
        printf("\n# users=%d\n", n);
        free(sessions);
        return 0;
    }

    if (w.heading)
        print_heading(&w);
    if (w.needBoot && !w.mineOnly) {
        char text[32];

        session_time_text(session_boot_time(), text, sizeof text);
        print_line(&w, "", ' ', "system boot", text, "", "", "");
    }
    if (w.needUsers) {
        for (i = 0; i < n; i++) {
            char text[32], pid[24];

            if (w.mineOnly && (mine == NULL || strcmp(sessions[i].tty, mine) != 0))
                continue;
            session_time_text(sessions[i].start, text, sizeof text);
            snprintf(pid, sizeof pid, "%lu", sessions[i].pid);
            print_line(&w, sessions[i].user, '?', sessions[i].tty, text, "  ?", pid, "");
        }
    }
    free(sessions);
    fflush(stdout);
    return 0;
}
