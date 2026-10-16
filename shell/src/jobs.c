#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jobs.h"
#include <tlhelp32.h>
#include "cmdline.h"
#include "command.h"
#include "variables.h"

typedef struct {
    int     number;
    HANDLE* processes;
    size_t  count;
    DWORD   pid;                // of the last process
    char*   label;
    int     done;
    DWORD   exitCode;
} Job;

static Job*   jobs;
static size_t jobCount;
static int    interactive;

void jobs_set_interactive(int on) {
    interactive = on;
}

/** Notes that the job has ended, when it has. */
static void refresh(Job* job) {
    if (job->done)
        return;
    if (WaitForSingleObject(job->processes[job->count - 1], 0) == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(job->processes[job->count - 1], &job->exitCode))
            job->exitCode = 1;
        job->done = 1;
    }
}

static void remove_job(size_t index) {
    Job* job = &jobs[index];
    size_t i;

    for (i = 0; i < job->count; i++)
        CloseHandle(job->processes[i]);
    free(job->processes);
    free(job->label);
    memmove(job, job + 1, (jobCount - index - 1) * sizeof *job);
    jobCount--;
}

int jobs_add(HANDLE* processes, size_t count, const char* label) {
    Job* grown;
    Job* job;
    char text[32];
    int number = 1;
    size_t i;

    if (count == 0) {
        free(processes);
        return -1;
    }
    for (i = 0; i < jobCount; i++)
        if (jobs[i].number >= number)
            number = jobs[i].number + 1;
    grown = realloc(jobs, (jobCount + 1) * sizeof *grown);
    if (grown == NULL) {
        free(processes);
        return -1;
    }
    jobs = grown;
    job = &jobs[jobCount++];
    job->number = number;
    job->processes = processes;
    job->count = count;
    job->pid = GetProcessId(processes[count - 1]);
    job->label = _strdup(label != NULL ? label : "");
    job->done = 0;
    job->exitCode = 0;

    snprintf(text, sizeof text, "%lu", (unsigned long)job->pid);
    var_set("!", text);
    if (interactive)
        printf("[%d] %lu\n", number, (unsigned long)job->pid);
    return number;
}

int jobs_start_shell(char* const argv[], const char* label) {
    SECURITY_ATTRIBUTES inheritable = { sizeof inheritable, NULL, TRUE };
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE nul;
    HANDLE* processes;
    char* command = cmdline_build(argv);
    int number;

    if (command == NULL)
        return -1;
    nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, NULL);
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    SetHandleInformation(si.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(si.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    ZeroMemory(&pi, sizeof pi);
    fflush(stdout);
    if (!CreateProcessA(argv[0], command, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "Error: Failed to start the background job\n");
        free(command);
        if (nul != INVALID_HANDLE_VALUE)
            CloseHandle(nul);
        return -1;
    }
    free(command);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    CloseHandle(pi.hThread);
    processes = malloc(sizeof *processes);
    if (processes == NULL) {
        CloseHandle(pi.hProcess);
        return -1;
    }
    processes[0] = pi.hProcess;
    number = jobs_add(processes, 1, label);
    return number;
}

static int is_suspended(HANDLE process);

static const char* state_text(const Job* job, char* buffer, size_t size) {
    if (!job->done)
        return is_suspended(job->processes[job->count - 1]) ? "Stopped" : "Running";
    if (job->exitCode == 0)
        return "Done";
    snprintf(buffer, size, "Exit %lu", (unsigned long)job->exitCode);
    return buffer;
}

static char marker(size_t index) {
    return index + 1 == jobCount ? '+' : (index + 2 == jobCount ? '-' : ' ');
}

int jobs_builtin_jobs(char* const* argv) {
    size_t i = 0;
    int showPid = 0, onlyPid = 0, a;

    for (a = 1; argv[a] != NULL && argv[a][0] == '-' && argv[a][1] != '\0'; a++) {
        if (strcmp(argv[a], "-l") == 0) {
            showPid = 1;
        } else if (strcmp(argv[a], "-p") == 0) {
            onlyPid = 1;
        } else {
            fprintf(stderr, "jobs: %s: invalid option\n", argv[a]);
            return 2;
        }
    }
    while (i < jobCount) {
        char buffer[32];
        const char* state;

        refresh(&jobs[i]);
        state = state_text(&jobs[i], buffer, sizeof buffer);
        if (onlyPid)
            printf("%lu\n", (unsigned long)jobs[i].pid);
        else if (showPid)
            printf("[%d]%c %lu %-22s%s\n", jobs[i].number, marker(i), (unsigned long)jobs[i].pid, state, jobs[i].label);
        else
            printf("[%d]%c  %-22s%s\n", jobs[i].number, marker(i), state, jobs[i].label);
        if (jobs[i].done)
            remove_job(i);
        else
            i++;
    }
    fflush(stdout);
    return 0;
}

void jobs_report_finished(void) {
    size_t i = 0;

    if (!interactive)
        return;

    while (i < jobCount) {
        refresh(&jobs[i]);
        if (jobs[i].done) {
            char buffer[32];
            const char* state = state_text(&jobs[i], buffer, sizeof buffer);

            printf("[%d]%c  %-22s%s\n", jobs[i].number, marker(i), state, jobs[i].label);
            remove_job(i);
        } else {
            i++;
        }
    }
    fflush(stdout);
}

/** Waits for the job at index. Returns its status, or -1 when Ctrl+C interrupted the wait. */
static int wait_for(size_t index) {
    Job* job = &jobs[index];

    while (!job->done) {
        if (WaitForSingleObject(job->processes[job->count - 1], 50) == WAIT_OBJECT_0)
            refresh(job);
        else if (command_was_interrupted())
            return -1;
    }
    return (int)(job->exitCode & 0xFF);
}

int jobs_builtin_wait(char* const* argv) {
    int status = 0, i;

    if (argv[1] == NULL) {
        while (jobCount > 0) {
            if (wait_for(0) < 0)
                return 130;
            remove_job(0);
        }
        return 0;
    }
    for (i = 1; argv[i] != NULL; i++) {
        const char* id = argv[i];
        size_t k;
        int found = 0;

        for (k = 0; k < jobCount; k++) {
            int match = id[0] == '%' ? jobs[k].number == atoi(id + 1) : (unsigned long)jobs[k].pid == strtoul(id, NULL, 10);

            if (match) {
                found = 1;
                status = wait_for(k);
                if (status < 0)
                    return 130;
                remove_job(k);
                break;
            }
        }
        if (!found) {
            if (id[0] == '%')
                fprintf(stderr, "wait: %s: no such job\n", id);
            else
                fprintf(stderr, "wait: pid %s is not a child of this shell\n", id);
            status = 127;
        }
    }
    return status;
}

/* ---- job control: fg, bg, disown and the job arguments of kill --------------------------------- */

/** Whether the process is suspended (kill -STOP): a thread of it has a suspend count above 0. */
static int is_suspended(HANDLE process) {
    DWORD pid = GetProcessId(process);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry;
    int suspended = 0;

    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    entry.dwSize = sizeof entry;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == pid) {
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);

                if (thread != NULL) {
                    DWORD before = SuspendThread(thread);

                    if (before != (DWORD)-1) {
                        ResumeThread(thread);
                        suspended = before > 0;
                    }
                    CloseHandle(thread);
                    break;
                }
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return suspended;
}

typedef LONG (NTAPI* NtProcessFunction)(HANDLE);

/** Lets a suspended job go on (what kill -CONT does). */
static void resume_job(const Job* job) {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    NtProcessFunction resume = ntdll != NULL ? (NtProcessFunction)(void*)GetProcAddress(ntdll, "NtResumeProcess") : NULL;
    size_t i;

    for (i = 0; resume != NULL && i < job->count; i++)
        if (is_suspended(job->processes[i]))
            resume(job->processes[i]);
}

static int all_digits(const char* s) {
    if (*s == '\0')
        return 0;
    for (; *s != '\0'; s++)
        if (*s < '0' || *s > '9')
            return 0;
    return 1;
}

/**
 * The job a spec names: %N or N (the number), %% or %+ (the current one: the last started), %- (the one before),
 * %string (the label starts with it), %?string (the label has it); no spec is the current job. Returns its
 * index, or -1 after a message in the name of `command`.
 */
static int find_job(const char* command, const char* spec) {
    const char* s = spec;
    size_t i;

    if (spec == NULL || strcmp(spec, "%%") == 0 || strcmp(spec, "%+") == 0 || strcmp(spec, "+") == 0) {
        if (jobCount == 0) {
            fprintf(stderr, "%s: current: no such job\n", command);
            return -1;
        }
        return (int)jobCount - 1;
    }
    if (strcmp(spec, "%-") == 0 || strcmp(spec, "-") == 0) {
        if (jobCount < 2) {
            fprintf(stderr, "%s: previous: no such job\n", command);
            return -1;
        }
        return (int)jobCount - 2;
    }
    if (*s == '%')
        s++;
    if (all_digits(s)) {
        int number = atoi(s);

        for (i = 0; i < jobCount; i++)
            if (jobs[i].number == number)
                return (int)i;
    } else if (spec[0] == '%' && s[0] == '?' && s[1] != '\0') {
        for (i = 0; i < jobCount; i++)
            if (strstr(jobs[i].label, s + 1) != NULL)
                return (int)i;
    } else if (spec[0] == '%' && s[0] != '\0') {
        for (i = 0; i < jobCount; i++)
            if (strncmp(jobs[i].label, s, strlen(s)) == 0)
                return (int)i;
    }
    fprintf(stderr, "%s: %s: no such job\n", command, spec);
    return -1;
}

/** The label of a job as it was typed, without the & that was added to it. */
static void print_label(const Job* job) {
    size_t length = strlen(job->label);

    if (length >= 2 && job->label[length - 1] == '&' && job->label[length - 2] == ' ')
        length -= 2;
    printf("%.*s\n", (int)length, job->label);
    fflush(stdout);
}

int jobs_builtin_fg(char* const* argv) {
    int index = find_job("fg", argv[1]);
    Job* job;
    int status;
    size_t i;

    if (index < 0)
        return 1;
    job = &jobs[index];
    print_label(job);
    resume_job(job);
    while (!job->done) {
        if (WaitForSingleObject(job->processes[job->count - 1], 50) == WAIT_OBJECT_0) {
            refresh(job);
        } else if (command_was_interrupted()) {
            // Ctrl+C belongs to the job that is in the foreground
            for (i = 0; i < job->count; i++)
                TerminateProcess(job->processes[i], 130);
            WaitForSingleObject(job->processes[job->count - 1], 1000);
            job->exitCode = 130;
            job->done = 1;
        }
    }
    status = (int)(job->exitCode & 0xFF);
    remove_job((size_t)index);
    return status;
}

int jobs_builtin_bg(char* const* argv) {
    int index = find_job("bg", argv[1]);
    Job* job;

    if (index < 0)
        return 1;
    job = &jobs[index];
    refresh(job);
    if (job->done) {
        fprintf(stderr, "bg: job %d has terminated\n", job->number);
        return 1;
    }
    if (!is_suspended(job->processes[job->count - 1])) {
        fprintf(stderr, "bg: job %d already in background\n", job->number);
        return 0;
    }
    resume_job(job);
    printf("[%d]%c %s\n", job->number, marker((size_t)index), job->label);
    fflush(stdout);
    return 0;
}

int jobs_builtin_disown(char* const* argv) {
    int status = 0, i;

    if (argv[1] == NULL) {
        int index = find_job("disown", NULL);

        if (index < 0)
            return 1;
        remove_job((size_t)index);
        return 0;
    }
    if (strcmp(argv[1], "-a") == 0) {
        while (jobCount > 0)
            remove_job(0);
        return 0;
    }
    for (i = 1; argv[i] != NULL; i++) {
        int index = find_job("disown", argv[i]);

        if (index < 0)
            status = 1;
        else
            remove_job((size_t)index);
    }
    return status;
}

int jobs_has_job_argument(char* const* argv) {
    int i;

    for (i = 1; argv[i] != NULL; i++)
        if (argv[i][0] == '%')
            return 1;
    return 0;
}

char** jobs_expand_kill_arguments(char* const* argv) {
    size_t capacity = 8, count = 0;
    char** result = malloc(capacity * sizeof *result);
    int i;

    if (result == NULL)
        return NULL;
    for (i = 0; argv[i] != NULL; i++) {
        char text[32];
        size_t k, added = 1;
        int index = -1;

        if (argv[i][0] == '%' && i > 0) {
            index = find_job("kill", argv[i]);
            if (index < 0) {
                jobs_free_arguments(result, count);
                return NULL;
            }
            added = jobs[index].count;
        }
        while (count + added + 1 > capacity) {
            char** grown = realloc(result, (capacity *= 2) * sizeof *grown);

            if (grown == NULL) {
                jobs_free_arguments(result, count);
                return NULL;
            }
            result = grown;
        }
        if (index < 0) {
            result[count++] = _strdup(argv[i]);
        } else {
            for (k = 0; k < jobs[index].count; k++) {
                snprintf(text, sizeof text, "%lu", (unsigned long)GetProcessId(jobs[index].processes[k]));
                result[count++] = _strdup(text);
            }
        }
    }
    result[count] = NULL;
    return result;
}

void jobs_free_arguments(char** arguments, size_t count) {
    size_t i;

    if (arguments == NULL)
        return;
    if (count == (size_t)-1)
        for (count = 0; arguments[count] != NULL; count++)
            ;
    for (i = 0; i < count; i++)
        free(arguments[i]);
    free(arguments);
}
