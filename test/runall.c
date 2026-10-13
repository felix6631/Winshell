/*
 * Runs every *_test.exe of a directory, several at a time, and reports the ones that failed.
 *
 *   runall DIR          a failed program is shown with all its output, the others with their last line
 *   WS_VERBOSE=1        show the whole output of every program
 *   WS_JOBS=N           programs running at once (default: half of the processors, at least 2)
 *
 * The programs do not share anything (each has its own work directory below DIR), so they can overlap.
 * The slow ones are started first, so that they do not end up alone at the end. The exit code is 0
 * when all of the programs passed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_PROGRAMS 128

typedef struct {
    char name[64];
    char path[MAX_PATH];
    char log[MAX_PATH];
    HANDLE process;
    DWORD started;
    DWORD elapsed;
    DWORD exitCode;
    int slow;
} Program;

static Program programs[MAX_PROGRAMS];
static int programCount;

/** The programs that take long when they run alone. */
static int is_slow(const char* name) {
    static const char* const slow[] = { "equiv_test", "limits_test", "shell_lists_test", "shell_run_test" };
    size_t i;
    for (i = 0; i < sizeof slow / sizeof slow[0]; i++)
        if (strcmp(name, slow[i]) == 0)
            return 1;
    return 0;
}

static int by_order(const void* a, const void* b) {
    const Program* x = a;
    const Program* y = b;
    if (x->slow != y->slow)
        return y->slow - x->slow;
    return strcmp(x->name, y->name);
}

static int by_name(const void* a, const void* b) {
    return strcmp(((const Program*)a)->name, ((const Program*)b)->name);
}

static void find_programs(const char* directory) {
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE find;

    snprintf(pattern, sizeof pattern, "%s\\*_test.exe", directory);
    find = FindFirstFileA(pattern, &found);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        Program* p;
        size_t length = strlen(found.cFileName);
        if (programCount == MAX_PROGRAMS)
            break;
        p = &programs[programCount++];
        snprintf(p->name, sizeof p->name, "%.*s", (int)(length - 4), found.cFileName);
        snprintf(p->path, sizeof p->path, "%s\\%s", directory, found.cFileName);
        snprintf(p->log, sizeof p->log, "%s\\work\\%s.log", directory, p->name);
        p->slow = is_slow(p->name);
    } while (FindNextFileA(find, &found));
    FindClose(find);
}

static int start(Program* p) {
    SECURITY_ATTRIBUTES inherit = { sizeof inherit, NULL, TRUE };
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char command[MAX_PATH + 4];
    HANDLE log = CreateFileA(p->log, GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (log == INVALID_HANDLE_VALUE)
        return 0;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = log;
    si.hStdError = log;
    snprintf(command, sizeof command, "\"%s\"", p->path);
    if (!CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(log);
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(log);
    p->process = pi.hProcess;
    p->started = GetTickCount();
    return 1;
}

static char* read_log(const Program* p) {
    FILE* f = fopen(p->log, "rb");
    long size;
    char* text;

    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = malloc((size_t)size + 1);
    if (text != NULL) {
        size = (long)fread(text, 1, (size_t)size, f);
        text[size] = '\0';
    }
    fclose(f);
    return text;
}

/** The last non-empty line of the text. */
static const char* last_line(char* text) {
    size_t length = strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r'))
        text[--length] = '\0';
    while (length > 0 && text[length - 1] != '\n')
        length--;
    return text + length;
}

int main(int argc, char* argv[]) {
    SYSTEM_INFO system;
    const char* jobsText = getenv("WS_JOBS");
    const char* verboseText = getenv("WS_VERBOSE");
    int verbose = verboseText != NULL && verboseText[0] != '\0' && verboseText[0] != '0';
    int jobs, next = 0, running = 0, failed = 0, i;
    char work[MAX_PATH];
    DWORD begin = GetTickCount();
    HANDLE handles[MAXIMUM_WAIT_OBJECTS];
    int owner[MAXIMUM_WAIT_OBJECTS];

    if (argc < 2) {
        fprintf(stderr, "usage: runall DIRECTORY\n");
        return 2;
    }
    GetSystemInfo(&system);
    jobs = jobsText != NULL ? atoi(jobsText) : (int)system.dwNumberOfProcessors / 2;
    if (jobs < 2)
        jobs = 2;
    if (jobs > MAXIMUM_WAIT_OBJECTS)
        jobs = MAXIMUM_WAIT_OBJECTS;

    find_programs(argv[1]);
    if (programCount == 0) {
        fprintf(stderr, "no test programs in %s\n", argv[1]);
        return 2;
    }
    snprintf(work, sizeof work, "%s\\work", argv[1]);
    CreateDirectoryA(work, NULL);
    qsort(programs, (size_t)programCount, sizeof programs[0], by_order);

    while (next < programCount || running > 0) {
        while (next < programCount && running < jobs) {
            Program* p = &programs[next++];
            if (start(p)) {
                handles[running] = p->process;
                owner[running] = (int)(p - programs);
                running++;
            } else {
                p->exitCode = 255;
                p->process = NULL;
            }
        }
        if (running > 0) {
            DWORD which = WaitForMultipleObjects((DWORD)running, handles, FALSE, INFINITE) - WAIT_OBJECT_0;
            Program* p = &programs[owner[which]];
            GetExitCodeProcess(p->process, &p->exitCode);
            p->elapsed = GetTickCount() - p->started;
            CloseHandle(p->process);
            p->process = NULL;
            running--;
            handles[which] = handles[running];
            owner[which] = owner[running];
        }
    }

    qsort(programs, (size_t)programCount, sizeof programs[0], by_name);
    for (i = 0; i < programCount; i++) {
        Program* p = &programs[i];
        char* text = read_log(p);

        if (p->exitCode != 0)
            failed++;
        if (text == NULL) {
            printf("%s: no output (exit code %lu)\n", p->name, (unsigned long)p->exitCode);
        } else if (verbose || p->exitCode != 0) {
            printf("%s", text);
            if (p->exitCode != 0)
                printf("%s: exit code %lu\n", p->name, (unsigned long)p->exitCode);
        } else {
            printf("%s (%.1fs)\n", last_line(text), p->elapsed / 1000.0);
        }
        free(text);
    }

    printf("\n");
    if (failed > 0) {
        printf("FAILED:");
        for (i = 0; i < programCount; i++)
            if (programs[i].exitCode != 0)
                printf(" %s", programs[i].name);
        printf("\n");
        return 1;
    }
    printf("all %d test programs passed in %.1fs\n", programCount, (GetTickCount() - begin) / 1000.0);
    return 0;
}
