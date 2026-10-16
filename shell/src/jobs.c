#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jobs.h"
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

static const char* state_text(const Job* job, char* buffer, size_t size) {
    if (!job->done)
        return "Running";
    if (job->exitCode == 0)
        return "Done";
    snprintf(buffer, size, "Exit %lu", (unsigned long)job->exitCode);
    return buffer;
}

static char marker(size_t index) {
    return index + 1 == jobCount ? '+' : (index + 2 == jobCount ? '-' : ' ');
}

int jobs_builtin_jobs(void) {
    size_t i = 0;

    while (i < jobCount) {
        char buffer[32];
        const char* state;

        refresh(&jobs[i]);
        state = state_text(&jobs[i], buffer, sizeof buffer);
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
