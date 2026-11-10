/*
 * pinky: a lightweight finger, as GNU pinky does. The short format lists the open shells of Winshell (the sessions of
 * common/sessions.h) with the login name, the real name, the terminal, the idle time (unknown: ?), the time the shell was
 * opened and where from (nowhere: nothing). The long format (-l) tells about a user: the real name, the home directory and
 * the shell, and the contents of the files .project and .plan of the home directory. The real name is the full name of the
 * account (its name when it has none), the shell is the shell.exe of Winshell. A FILE operand does not exist: there is no
 * utmp file.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <lm.h>
#include <userenv.h>
#include "args.h"
#include "sessions.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "pinky"

static void print_usage(void) {
    puts("Usage: pinky [OPTION]... [USER]...\n"
         "\n"
         "  -l              produce long format output for the specified USERs\n"
         "  -b              omit the user's home directory and shell in long format\n"
         "  -h              omit the user's project file in long format\n"
         "  -p              omit the user's plan file in long format\n"
         "  -s              do short format output, this is the default\n"
         "  -f              omit the line of column headings in short format\n"
         "  -w              omit the user's full name in short format\n"
         "  -i              omit the user's full name and remote host in short format\n"
         "  -q              omit the user's full name, remote host and idle time\n"
         "                  in short format\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "A lightweight 'finger' program;  print user information.\n"
         "The sessions are the open shells of Winshell (there is no /var/run/utmp).");
}

static const ArgsOption options[] = {
    { 'l', 'l', NULL, ARGS_NONE }, { 'b', 'b', NULL, ARGS_NONE }, { 'h', 'h', NULL, ARGS_NONE }, { 'p', 'p', NULL, ARGS_NONE },
    { 's', 's', NULL, ARGS_NONE }, { 'f', 'f', NULL, ARGS_NONE }, { 'w', 'w', NULL, ARGS_NONE }, { 'i', 'i', NULL, ARGS_NONE },
    { 'q', 'q', NULL, ARGS_NONE }, { 0 }
};
static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int longFormat, heading, fullName, idle, where, homeAndShell, project, plan;
} Pinky;

static int on_option(int id, const char* value, void* context) {
    Pinky* p = (Pinky*)context;

    (void)value;
    switch (id) {
    case 'l': p->longFormat = 1; break;
    case 'b': p->homeAndShell = 0; break;
    case 'h': p->project = 0; break;
    case 'p': p->plan = 0; break;
    case 's': p->longFormat = 0; break;
    case 'f': p->heading = 0; break;
    case 'w': p->fullName = 0; break;
    case 'i': p->fullName = 0; p->where = 0; break;
    case 'q': p->fullName = 0; p->where = 0; p->idle = 0; break;
    default: break;
    }
    return 0;
}

/** The full name of an account (NULL when it has none, or is not found). */
static char* full_name(const char* user, char* out, size_t size) {
    wchar_t wide[256];
    USER_INFO_2* info = NULL;
    char* result = NULL;

    if (MultiByteToWideChar(CP_UTF8, 0, user, -1, wide, 256) == 0)
        return NULL;
    if (NetUserGetInfo(NULL, wide, 2, (LPBYTE*)&info) == NERR_Success && info != NULL) {
        if (info->usri2_full_name != NULL && info->usri2_full_name[0] != L'\0' &&
            WideCharToMultiByte(CP_UTF8, 0, info->usri2_full_name, -1, out, (int)size, NULL, NULL) > 0)
            result = out;
        NetApiBufferFree(info);
    }
    return result;
}

/** Whether the account exists. */
static int account_exists(const char* user) {
    wchar_t wide[256];
    USER_INFO_0* info = NULL;
    int found = 0;

    if (MultiByteToWideChar(CP_UTF8, 0, user, -1, wide, 256) == 0)
        return 0;
    if (NetUserGetInfo(NULL, wide, 0, (LPBYTE*)&info) == NERR_Success && info != NULL) {
        found = 1;
        NetApiBufferFree(info);
    }
    return found;
}

static void home_directory(const char* user, char* out, size_t size) {
    char native[MAX_PATH], current[256];
    DWORD currentSize = sizeof current, profiles = sizeof native;

    out[0] = '\0';
    if (GetUserNameA(current, &currentSize) && _stricmp(current, user) == 0 && getenv("USERPROFILE") != NULL) {
        snprintf(native, sizeof native, "%s", getenv("USERPROFILE"));
    } else if (GetProfilesDirectoryA(native, &profiles)) {
        size_t n = strlen(native);

        snprintf(native + n, sizeof native - n, "\\%s", user);
    } else {
        return;
    }
    if (vpath_from_native(native, out, size) != 0)
        snprintf(out, size, "%s", native);
}

static void shell_path(char* out, size_t size) {
    char native[MAX_PATH], *slash;

    out[0] = '\0';
    if (GetModuleFileNameA(NULL, native, sizeof native) == 0)
        return;
    slash = strrchr(native, '\\');
    if (slash == NULL)
        return;
    snprintf(slash + 1, sizeof native - (size_t)(slash + 1 - native), "shell.exe");
    if (vpath_from_native(native, out, size) != 0)
        snprintf(out, size, "%s", native);
}

/** Writes the lines of a file of the home directory (.project, .plan) after a heading. */
static void print_file(const char* home, const char* name, const char* heading) {
    char virtualPath[VPATH_MAX * 2], native[VPATH_MAX];
    FILE* f;
    int c;
    int first = 1;

    snprintf(virtualPath, sizeof virtualPath, "%s/%s", home, name);
    if (vpath_to_native(virtualPath, native, sizeof native) != 0)
        return;
    f = fopen(native, "rb");
    if (f == NULL)
        return;
    while ((c = fgetc(f)) != EOF) {
        if (first) {
            printf("%s", heading);
            first = 0;
        }
        putchar(c);
    }
    fclose(f);
}

static void print_long(const Pinky* p, const char* user) {
    char name[256], home[VPATH_MAX], shell[VPATH_MAX];
    const char* real;

    home_directory(user, home, sizeof home);
    shell_path(shell, sizeof shell);
    real = full_name(user, name, sizeof name);
    printf("Login name: %-28sIn real life:  %s\n", user, real != NULL ? real : user);
    if (p->homeAndShell)
        printf("Directory: %-29sShell:  %s\n", home, shell);
    if (p->project)
        print_file(home, ".project", "Project: ");
    if (p->plan)
        print_file(home, ".plan", "Plan:\n");
}

int main(int argc, char* argv[]) {
    Pinky p;
    const char** operands;
    int count, result, n, i, k;
    Session* sessions = NULL;

    memset(&p, 0, sizeof p);
    p.heading = p.fullName = p.idle = p.where = p.homeAndShell = p.project = p.plan = 1;
    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, &p, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    if (p.longFormat) {
        if (count == 0) {
            fprintf(stderr, PROGRAM ": no username specified; at least one must be specified when using -l\n");
            args_usage_error(&spec);
            return 1;
        }
        for (i = 0; i < count; i++) {
            if (!account_exists(operands[i])) {
                printf("Login name: %-28sIn real life:  ???\n", operands[i]);
                continue;
            }
            print_long(&p, operands[i]);
            putchar('\n');
        }
        return 0;
    }

    if (p.heading) {
        printf("Login    ");
        if (p.fullName)
            printf("Name                 ");
        else
            printf(" ");
        printf("TTY      ");
        if (p.idle)
            printf("Idle   ");
        printf("When");
        printf(p.where ? "         Where" : "        ");
        putchar('\n');
    }
    n = session_list(&sessions);
    for (i = 0; i < n; i++) {
        char time[32], name[256];
        const char* real;

        if (count > 0) {
            for (k = 0; k < count; k++) {
                if (strcmp(operands[k], sessions[i].user) == 0)
                    break;
            }
            if (k == count)
                continue;
        }
        session_time_text(sessions[i].start, time, sizeof time);
        printf("%-8s", sessions[i].user);
        if (p.fullName) {
            real = full_name(sessions[i].user, name, sizeof name);
            printf(" %-19.19s", real != NULL ? real : sessions[i].user);
        }
        putchar(' ');
        printf(" %c%-8s", '?', sessions[i].tty);
        if (p.idle)
            printf(" %-6s", "?");
        printf(" %s", time);
        putchar('\n');
    }
    free(sessions);
    fflush(stdout);
    return 0;
}
