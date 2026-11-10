/*
 * timeout: runs a command and ends it when it runs too long, as GNU timeout does. Windows has no signals, so a signal is
 * mapped as kill maps it: KILL ends the command (and what it started) at once; STOP, TSTP, TTIN and TTOU suspend it;
 * CHLD, URG, WINCH, CONT and 0 do nothing; any other signal is a Ctrl+Break for a command with a console and a request to
 * close the windows of the command, and when neither reaches it the command is ended (exit status 128 + the signal).
 * Unless --foreground is given the command runs in a job, so that everything it started is ended with it.
 * The exit status is 124 when the command timed out (or the status of the command with --preserve-status), 137 when it
 * had to be killed, 125 when timeout itself failed, 126 and 127 when the command cannot be run, else the command's.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "cmdline.h"
#include "fsutil.h"
#include "program.h"

#define VERSION "0.1"
#define PROGRAM "timeout"

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

static void print_usage(void) {
    puts("Usage: timeout [OPTION] DURATION COMMAND [ARG]...\n"
         "  or:  timeout [OPTION]\n"
         "Start COMMAND, and kill it if still running after DURATION.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "      --preserve-status\n"
         "                 exit with the same status as COMMAND, even when the\n"
         "                   command times out\n"
         "      --foreground\n"
         "                 when not running timeout directly from a shell prompt,\n"
         "                   allow COMMAND to read from the TTY and get TTY signals;\n"
         "                   in this mode, children of COMMAND will not be timed out\n"
         "  -k, --kill-after=DURATION\n"
         "                 also send a KILL signal if COMMAND is still running\n"
         "                   this long after the initial signal was sent\n"
         "  -s, --signal=SIGNAL\n"
         "                 specify the signal to be sent on timeout;\n"
         "                   SIGNAL may be a name like 'HUP' or a number;\n"
         "                   see 'kill -l' for a list of signals\n"
         "  -v, --verbose  diagnose to stderr any signal sent upon timeout\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "DURATION is a floating point number with an optional suffix:\n"
         "'s' for seconds (the default), 'm' for minutes, 'h' for hours or 'd' for days.\n"
         "A duration of 0 disables the associated timeout.\n"
         "\n"
         "Upon timeout, send the TERM signal to COMMAND, if no other SIGNAL specified.\n"
         "The TERM signal kills any process that does not block or catch that signal.\n"
         "It may be necessary to use the KILL signal, since this signal can't be caught.\n"
         "\n"
         "Windows has no signals: KILL ends the command and its children, STOP suspends it, CHLD/URG/WINCH/CONT/0 do\n"
         "nothing, any other signal is a Ctrl+Break and a request to close the windows (or ends a command with neither).\n"
         "\n"
         "Exit status:\n"
         "  124  if COMMAND times out, and --preserve-status is not specified\n"
         "  125  if the timeout command itself fails\n"
         "  126  if COMMAND is found but cannot be invoked\n"
         "  127  if COMMAND cannot be found\n"
         "  137  if COMMAND (or timeout itself) is sent the KILL (9) signal (128+9)\n"
         "  -    the exit status of COMMAND otherwise");
}

enum { OPT_FOREGROUND = 256, OPT_PRESERVE };

static const ArgsOption options[] = {
    { OPT_FOREGROUND, 0, "foreground", ARGS_NONE },
    { OPT_PRESERVE, 0, "preserve-status", ARGS_NONE },
    { 'k', 'k', "kill-after", ARGS_REQUIRED },
    { 's', 's', "signal", ARGS_REQUIRED },
    { 'v', 'v', "verbose", ARGS_NONE },
    { 0 }
};

// the first operand ends the options: what follows the duration belongs to the command
static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 125, 1, 0 };

typedef struct {
    int         foreground;
    int         preserve;
    int         verbose;
    int         signal;                 // the signal to send on timeout
    double      killAfter;              // seconds; 0 for never
} Timeout;

static int signal_number(const char* text) {
    int i;

    if (*text >= '0' && *text <= '9') {
        char* end;
        long n = strtol(text, &end, 10);

        return *end == '\0' && n >= 0 && n <= 64 ? (int)n : -1;
    }
    if (_strnicmp(text, "SIG", 3) == 0)
        text += 3;
    for (i = 0; i < SIGNAL_COUNT; i++)
        if (_stricmp(text, signals[i].name) == 0)
            return signals[i].number;
    return -1;
}

static const char* signal_name(int number, char* buffer, size_t size) {
    int i;

    for (i = 0; i < SIGNAL_COUNT; i++)
        if (signals[i].number == number)
            return signals[i].name;
    snprintf(buffer, size, "%d", number);
    return buffer;
}

/** A duration: a number (as strtod reads it) and s, m, h or d. Returns 0 and the seconds (inf is allowed), or -1. */
static int parse_duration(const char* text, double* seconds) {
    char* end;
    double value;
    double scale = 1;

    if (*text == '\0')
        return -1;
    value = strtod(text, &end);
    if (end == text || value != value || value < 0)
        return -1;
    switch (*end) {
    case '\0': break;
    case 's': break;
    case 'm': scale = 60; break;
    case 'h': scale = 3600; break;
    case 'd': scale = 86400; break;
    default: return -1;
    }
    if (*end != '\0' && end[1] != '\0')
        return -1;
    *seconds = value * scale;
    return 0;
}

static void invalid_interval(const char* text) {
    fprintf(stderr, PROGRAM ": invalid time interval '%s'\n", text);
    args_usage_error(&spec);
}

static int on_option(int id, const char* value, void* context) {
    Timeout* t = (Timeout*)context;

    switch (id) {
    case OPT_FOREGROUND: t->foreground = 1; break;
    case OPT_PRESERVE: t->preserve = 1; break;
    case 'v': t->verbose = 1; break;
    case 'k':
        if (parse_duration(value, &t->killAfter) != 0) {
            invalid_interval(value);
            return -1;
        }
        break;
    case 's':
        t->signal = signal_number(value);
        if (t->signal < 0) {
            fprintf(stderr, PROGRAM ": '%s': invalid signal\n", value);
            args_usage_error(&spec);
            return -1;
        }
        break;
    default: break;
    }
    return 0;
}

/* ---- the command ----------------------------------------------------------------- */

static HANDLE interrupted;                  // set when timeout itself is interrupted (Ctrl+C, closing the console)

static BOOL WINAPI on_interrupt(DWORD type) {
    (void)type;
    SetEvent(interrupted);
    return TRUE;
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

typedef LONG (NTAPI* NtProcessFunction)(HANDLE);

static void suspend_process(HANDLE process) {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    NtProcessFunction suspend = ntdll != NULL ? (NtProcessFunction)(void*)GetProcAddress(ntdll, "NtSuspendProcess") : NULL;

    if (suspend != NULL)
        suspend(process);
}

static void end_command(HANDLE process, HANDLE job, int sig) {
    if (job != NULL)
        TerminateJobObject(job, (UINT)(128 + sig));
    else
        TerminateProcess(process, (UINT)(128 + sig));
}

/** Sends the signal to the command the way Windows allows it. */
static void send_signal(HANDLE process, HANDLE job, DWORD pid, int sig) {
    CloseRequest request;

    if (sig == 0 || sig == 17 || sig == 18 || sig == 23 || sig == 28)
        return;
    if (sig == 9) {
        end_command(process, job, sig);
        return;
    }
    if (sig == 19 || sig == 20 || sig == 21 || sig == 22) {
        suspend_process(process);
        return;
    }
    request.pid = pid;
    request.posted = 0;
    EnumWindows(close_window, (LPARAM)&request);
    if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid))
        request.posted++;
    if (request.posted == 0)
        end_command(process, job, sig);
}

/** Waits for the command up to the given seconds (negative: for ever). Returns 1 when it ended, 0 on time out, -1 when interrupted. */
static int wait_for(HANDLE process, double seconds) {
    HANDLE handles[2];
    DWORD ms = INFINITE, result;

    handles[0] = process;
    handles[1] = interrupted;
    if (seconds >= 0 && seconds * 1000.0 < 4294967000.0)
        ms = (DWORD)ceil(seconds * 1000.0);
    result = WaitForMultipleObjects(2, handles, FALSE, ms);
    if (result == WAIT_OBJECT_0)
        return 1;
    if (result == WAIT_OBJECT_0 + 1)
        return -1;
    return 0;
}

static int exit_status(DWORD code) {
    if (code > 255)
        return (int)(code & 0xFF) != 0 ? (int)(code & 0xFF) : 1;
    return (int)code;
}

int main(int argc, char* argv[]) {
    Timeout t;
    const char** operands;
    int count, result;
    double seconds;
    char path[MAX_PATH * 2];
    char* command;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE job = NULL, process;
    DWORD code = 0;
    int timedOut = 0, killed = 0, wait;
    char nameBuffer[16];

    memset(&t, 0, sizeof t);
    t.signal = 15;
    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, &t, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count < 2) {
        args_usage_error(&spec);
        return 125;
    }
    if (parse_duration(operands[0], &seconds) != 0) {
        invalid_interval(operands[0]);
        return 125;
    }

    if (program_find(operands[1], path, sizeof path) != 0) {
        fprintf(stderr, PROGRAM ": failed to run command '%s': No such file or directory\n", operands[1]);
        return 127;
    }
    command = cmdline_build((char* const*)(operands + 1));
    if (command == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return 125;
    }
    interrupted = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetConsoleCtrlHandler(on_interrupt, TRUE);

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    SetHandleInformation(si.hStdInput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(si.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(si.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    fflush(stdout);
    // its own process group, so that a Ctrl+Break can be sent to the command alone; suspended until it is in its job
    if (!CreateProcessA(path, command, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        fprintf(stderr, PROGRAM ": failed to run command '%s': %s\n", operands[1], fs_strerror(GetLastError()));
        free(command);
        return 126;
    }
    free(command);
    if (!t.foreground) {
        job = CreateJobObjectA(NULL, NULL);
        if (job != NULL && !AssignProcessToJobObject(job, pi.hProcess)) {
            CloseHandle(job);
            job = NULL;                     // already in a job that does not allow another: the command alone is ended
        }
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    process = pi.hProcess;

    wait = wait_for(process, seconds > 0 ? seconds : -1);
    if (wait == 0) {
        timedOut = 1;
        if (t.verbose)
            fprintf(stderr, PROGRAM ": sending signal %s to command '%s'\n", signal_name(t.signal, nameBuffer, sizeof nameBuffer), operands[1]);
        send_signal(process, job, pi.dwProcessId, t.signal);
        wait = wait_for(process, t.killAfter > 0 ? t.killAfter : -1);
        if (wait == 0) {
            if (t.verbose)
                fprintf(stderr, PROGRAM ": sending signal KILL to command '%s'\n", operands[1]);
            end_command(process, job, 9);
            killed = 1;
            wait = wait_for(process, -1);
        } else if (t.signal == 9) {
            killed = 1;
        }
    }
    if (wait < 0) {                         // timeout itself was interrupted: the command goes with it
        end_command(process, job, 2);
        WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
        return 130;
    }
    if (!GetExitCodeProcess(process, &code))
        code = 125;
    CloseHandle(process);
    if (job != NULL && !timedOut)
        CloseHandle(job);                   // the job ends what the command left running only when it timed out

    if (killed)
        return 137;
    if (timedOut)
        return t.preserve ? 128 + t.signal : 124;
    return exit_status(code);
}
