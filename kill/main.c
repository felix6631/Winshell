/*
 * kill: sends a signal to processes. Windows has no signals, so what a signal does is mapped to what the
 * process can be made to do:
 *   0                              only checks that the process exists and may be signalled
 *   STOP TSTP TTIN TTOU / CONT     suspends / resumes the process
 *   CHLD URG WINCH                 nothing (the default action of these is to ignore them)
 *   KILL                           ends the process at once, with the exit status 137
 *   any other                      asks a process with windows to close them (WM_CLOSE); a process without
 *                                  windows is ended, with the exit status 128 + the number of the signal
 * Process groups (a pid of 0 or a negative one) do not exist. The exit status is 0 when every process was
 * signalled and 1 when one was not.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define VERSION "0.1"
#define PROGRAM "kill"

typedef struct {
    const char* name;
    int         number;
} Signal;

// the numbers of Linux, so that a script means the same here
static const Signal signals[] = {
    { "HUP", 1 },   { "INT", 2 },   { "QUIT", 3 },  { "ILL", 4 },   { "TRAP", 5 },   { "ABRT", 6 },
    { "BUS", 7 },   { "FPE", 8 },   { "KILL", 9 },  { "USR1", 10 }, { "SEGV", 11 },  { "USR2", 12 },
    { "PIPE", 13 }, { "ALRM", 14 }, { "TERM", 15 }, { "STKFLT", 16 }, { "CHLD", 17 }, { "CONT", 18 },
    { "STOP", 19 }, { "TSTP", 20 }, { "TTIN", 21 }, { "TTOU", 22 }, { "URG", 23 },  { "XCPU", 24 },
    { "XFSZ", 25 }, { "VTALRM", 26 }, { "PROF", 27 }, { "WINCH", 28 }, { "POLL", 29 }, { "PWR", 30 },
    { "SYS", 31 }
};
#define SIGNAL_COUNT ((int)(sizeof signals / sizeof signals[0]))

enum { SIG_NONE = 0, SIG_KILL = 9, SIG_CONT = 18 };

static void print_usage(void) {
    puts("Usage: kill [-s SIGNAL | -SIGNAL | -n NUMBER] PID...\n"
         "       kill -l [SIGNAL | EXIT_STATUS]...\n"
         "       kill -L\n"
         "Send a signal (TERM by default) to the processes.\n"
         "\n"
         "  -s, --signal SIGNAL   the signal to send, by name (TERM, SIGTERM) or number\n"
         "  -n NUMBER             the signal to send, by number\n"
         "  -l, --list            list the signal names, or translate names and numbers\n"
         "  -L, --table           list the signal numbers with their names\n"
         "      --help            display this help and exit\n"
         "      --version         output version information and exit\n"
         "\n"
         "Windows has no signals: 0 only checks the process, STOP/TSTP and CONT suspend and resume it,\n"
         "KILL ends it at once, CHLD/URG/WINCH do nothing, and any other signal closes the windows of\n"
         "the process, or ends it when it has none (exit status 128 + the number of the signal).\n"
         "A process group (pid 0 or negative) does not exist here.\n"
         "\n"
         "Exit status: 0 if every process was signalled, 1 if one was not or on a wrong argument.");
}

static int is_number(const char* text) {
    if (*text == '\0')
        return 0;
    for (; *text != '\0'; text++)
        if (!isdigit((unsigned char)*text))
            return 0;
    return 1;
}

/** The number of a signal given by number or by name (with or without SIG, any case); -1 for a wrong one. */
static int signal_number(const char* text) {
    int i;

    if (is_number(text)) {
        long n = strlen(text) > 4 ? -1 : atol(text);
        return n >= 0 && n <= 64 ? (int)n : -1;
    }
    if (_strnicmp(text, "SIG", 3) == 0)
        text += 3;
    for (i = 0; i < SIGNAL_COUNT; i++)
        if (_stricmp(text, signals[i].name) == 0)
            return signals[i].number;
    return -1;
}

static const char* signal_name(int number) {
    int i;
    for (i = 0; i < SIGNAL_COUNT; i++)
        if (signals[i].number == number)
            return signals[i].name;
    return NULL;
}

/* ---- sending ----------------------------------------------------------- */

typedef LONG (NTAPI* NtProcessFunction)(HANDLE);

static NtProcessFunction ntdll_function(const char* name) {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    return ntdll != NULL ? (NtProcessFunction)(void*)GetProcAddress(ntdll, name) : NULL;
}

typedef struct {
    DWORD pid;
    int   posted;
} CloseRequest;

static BOOL CALLBACK close_window(HWND window, LPARAM parameter) {
    CloseRequest* request = (CloseRequest*)parameter;
    DWORD owner = 0;

    GetWindowThreadProcessId(window, &owner);
    if (owner == request->pid && GetWindow(window, GW_OWNER) == NULL && IsWindowVisible(window))
        if (PostMessageW(window, WM_CLOSE, 0, 0))
            request->posted++;
    return TRUE;
}

/** The text of an error of sending, as the errno names of Linux say it. */
static const char* failure_text(DWORD error) {
    return error == ERROR_ACCESS_DENIED ? "Operation not permitted" : "No such process";
}

/** Sends the signal. Returns 0, or the Windows error code. */
static DWORD send_signal(DWORD pid, int sig) {
    DWORD access = PROCESS_QUERY_LIMITED_INFORMATION;
    HANDLE process;
    DWORD result = 0, code;
    int suspend = sig == 19 || sig == 20 || sig == 21 || sig == 22;
    int ignored = sig == 17 || sig == 23 || sig == 28;

    if (sig == SIG_KILL || (!suspend && !ignored && sig != SIG_NONE && sig != SIG_CONT))
        access |= PROCESS_TERMINATE;
    if (suspend || sig == SIG_CONT)
        access |= PROCESS_SUSPEND_RESUME;

    process = OpenProcess(access, FALSE, pid);
    if (process == NULL)
        return GetLastError() == ERROR_ACCESS_DENIED ? ERROR_ACCESS_DENIED : ERROR_INVALID_PARAMETER;
    if (!GetExitCodeProcess(process, &code) || code != STILL_ACTIVE) {
        CloseHandle(process);
        return ERROR_INVALID_PARAMETER;
    }

    if (sig == SIG_NONE || ignored) {
        // existence is all that is asked
    } else if (suspend || sig == SIG_CONT) {
        NtProcessFunction function = ntdll_function(suspend ? "NtSuspendProcess" : "NtResumeProcess");
        if (function == NULL || function(process) < 0)
            result = ERROR_ACCESS_DENIED;
    } else {
        CloseRequest request;

        request.pid = pid;
        request.posted = 0;
        if (sig != SIG_KILL)
            EnumWindows(close_window, (LPARAM)&request);
        if (request.posted == 0 && !TerminateProcess(process, (UINT)(128 + sig)))
            result = GetLastError() == ERROR_ACCESS_DENIED ? ERROR_ACCESS_DENIED : ERROR_INVALID_PARAMETER;
    }
    CloseHandle(process);
    return result;
}

/* ---- listing ----------------------------------------------------------- */

static void list_names(void) {
    int i, column = 0;

    for (i = 0; i < SIGNAL_COUNT; i++) {
        int width = (int)strlen(signals[i].name) + 1;

        if (column + width > 78) {
            putchar('\n');
            column = 0;
        }
        printf("%s%s", column > 0 ? " " : "", signals[i].name);
        column += width;
    }
    putchar('\n');
}

static int translate(const char* text) {
    if (is_number(text)) {
        long n = strlen(text) > 4 ? -1 : atol(text);

        if (n > 128)
            n -= 128;                               // the exit status of a process that a signal ended
        if (n >= 1 && n <= 255 && signal_name((int)n) != NULL) {
            puts(signal_name((int)n));
            return 0;
        }
    } else {
        int number = signal_number(text);

        if (number >= 0) {
            printf("%d\n", number);
            return 0;
        }
    }
    fprintf(stderr, "%s: unknown signal name '%s'\n", PROGRAM, text);
    return 1;
}

/* ---- main -------------------------------------------------------------- */

static int bad_signal(const char* text) {
    fprintf(stderr, "%s: unknown signal name '%s'\n", PROGRAM, text);
    return 1;
}

int main(int argc, char* argv[]) {
    int sig = 15, i = 1, list = 0, status = 0;

    _setmode(_fileno(stdout), _O_BINARY);

    for (; i < argc; i++) {
        const char* a = argv[i];
        const char* value = NULL;

        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (a[0] != '-' || a[1] == '\0')
            break;
        if (strcmp(a, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(a, "--version") == 0) {
            printf("%s %s\n", PROGRAM, VERSION);
            return 0;
        }
        if (strcmp(a, "-l") == 0 || strcmp(a, "--list") == 0) {
            list = 1;
            i++;
            break;
        }
        if (strcmp(a, "-L") == 0 || strcmp(a, "--table") == 0) {
            int k;
            for (k = 0; k < SIGNAL_COUNT; k++)
                printf("%2d %s\n", signals[k].number, signals[k].name);
            return 0;
        }
        if (strcmp(a, "-q") == 0 || strcmp(a, "--queue") == 0) {
            i++;                                    // the value is not used: no queued signals here
            continue;
        }
        if (strcmp(a, "-s") == 0 || strcmp(a, "-n") == 0 || strcmp(a, "--signal") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: option requires an argument -- '%s'\n", PROGRAM, a);
                return 1;
            }
            value = argv[++i];
        } else if (strncmp(a, "--signal=", 9) == 0) {
            value = a + 9;
        } else if ((a[1] == 's' || a[1] == 'n') && a[2] != '\0' && signal_number(a + 1) < 0) {
            value = a + 2;                          // -sTERM, -n9 (but -SEGV is a signal of its own)
        }
        if (value == NULL)
            value = a + 1;                          // -9, -TERM, -SIGTERM
        sig = signal_number(value);
        if (sig < 0)
            return bad_signal(value);
    }

    if (list) {
        if (i >= argc) {
            list_names();
            return 0;
        }
        for (; i < argc; i++)
            if (translate(argv[i]) != 0)
                status = 1;
        return status;
    }

    if (i >= argc) {
        print_usage();
        return 1;
    }
    for (; i < argc; i++) {
        char* end;
        long pid = strtol(argv[i], &end, 10);
        DWORD error;

        if (*argv[i] == '\0' || *end != '\0') {
            fprintf(stderr, "%s: failed to parse argument: '%s'\n", PROGRAM, argv[i]);
            status = 1;
            continue;
        }
        if (pid <= 0) {
            fprintf(stderr, "%s: (%ld) - process groups are not supported\n", PROGRAM, pid);
            status = 1;
            continue;
        }
        error = send_signal((DWORD)pid, sig);
        if (error != 0) {
            fprintf(stderr, "%s: (%ld) - %s\n", PROGRAM, pid, failure_text(error));
            status = 1;
        }
    }
    return status;
}
