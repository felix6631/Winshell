/*
 * Runs the cases that test\gnu_compare.sh has collected: for each case the GNU tool and the tool of
 * this project get the same arguments and standard input, and their output and exit code have to be
 * the same. Starting a program from a native process takes a fraction of what it takes from the MSYS
 * bash, and the cases are independent, so they are run by several threads.
 *
 *   gnu_batch CASES GNU_DIR BIN_DIR
 *
 * The current directory is the one with the input files. A case file is a sequence of NUL terminated
 * fields:  TYPE LOCALE STDIN TOOL ARGC ARG...
 *   TYPE    "out" compares output and exit code, "status" only the exit code
 *   LOCALE  "C" or "U" (C.UTF-8) as LC_ALL for the GNU tool
 *   STDIN   a file name, or "-" for no input
 * Prints one line per case that differs; the exit code is their number (at most 255).
 * WS_JOBS sets the number of threads (default: the number of processors).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define TIMEOUT_MS 60000

typedef struct {
    int statusOnly;
    int utf8;
    const char* input;
    const char* tool;
    int argc;
    const char** argv;
    int expected;       // exit codes; -1 when the program could not run
    int got;
    int differs;
} Case;

static Case* cases;
static int caseCount;
static const char* gnuDir;
static const char* binDir;
static char* envC;
static char* envUtf8;
static volatile LONG nextCase;

/* ---- reading the cases ---------------------------------------------------------------- */

static char* data;
static size_t dataLength, dataPos;

static const char* field(void) {
    const char* start = data + dataPos;
    if (dataPos >= dataLength)
        return NULL;
    dataPos += strlen(start) + 1;
    return start;
}

static int load(const char* path) {
    FILE* f = fopen(path, "rb");
    size_t capacity = 0;

    if (f == NULL)
        return 0;
    fseek(f, 0, SEEK_END);
    dataLength = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(dataLength + 1);
    if (data == NULL || fread(data, 1, dataLength, f) != dataLength) {
        fclose(f);
        return 0;
    }
    data[dataLength] = '\0';
    fclose(f);

    for (;;) {
        const char* type = field();
        const char* locale;
        const char* input;
        const char* tool;
        const char* count;
        Case* c;
        int i;

        if (type == NULL)
            break;
        locale = field();
        input = field();
        tool = field();
        count = field();
        if (count == NULL)
            return 0;
        if (caseCount == (int)capacity) {
            capacity = capacity == 0 ? 1024 : capacity * 2;
            cases = realloc(cases, capacity * sizeof cases[0]);
            if (cases == NULL)
                return 0;
        }
        c = &cases[caseCount++];
        memset(c, 0, sizeof *c);
        c->statusOnly = strcmp(type, "status") == 0;
        c->utf8 = locale[0] == 'U';
        c->input = strcmp(input, "-") == 0 ? NULL : input;
        c->tool = tool;
        c->argc = atoi(count);
        c->argv = malloc(((size_t)c->argc + 1) * sizeof c->argv[0]);
        for (i = 0; i < c->argc; i++) {
            c->argv[i] = field();
            if (c->argv[i] == NULL)
                return 0;
        }
    }
    return 1;
}

/* ---- running one program -------------------------------------------------------------- */

typedef struct {
    char* text;
    size_t length, capacity;
} Text;

static void text_add(Text* t, const char* s, size_t n) {
    if (t->length + n + 1 > t->capacity) {
        t->capacity = (t->length + n + 1) * 2;
        t->text = realloc(t->text, t->capacity);
    }
    memcpy(t->text + t->length, s, n);
    t->length += n;
    t->text[t->length] = '\0';
}

/** Appends the argument quoted so that the C runtime of the program gets it back as it is. */
static void add_quoted(Text* line, const char* arg) {
    const char* p;
    size_t slashes = 0;

    text_add(line, "\"", 1);
    for (p = arg; *p != '\0'; p++) {
        if (*p == '\\') {
            slashes++;
            continue;
        }
        if (*p == '"') {
            for (; slashes > 0; slashes--)
                text_add(line, "\\\\", 2);
            text_add(line, "\\\"", 2);
        } else {
            for (; slashes > 0; slashes--)
                text_add(line, "\\", 1);
            text_add(line, p, 1);
        }
        slashes = 0;
    }
    for (; slashes > 0; slashes--)
        text_add(line, "\\\\", 2);
    text_add(line, "\"", 1);
}

/** Starts the program with the standard output in the file; returns its exit code or -1. */
static int run_program(const char* path, const Case* c, const char* outFile, char* env, Text* output) {
    SECURITY_ATTRIBUTES inherit = { sizeof inherit, NULL, TRUE };
    STARTUPINFOEXA si;
    PROCESS_INFORMATION pi;
    SIZE_T attributeSize = 0;
    HANDLE inherited[3];
    Text line = { NULL, 0, 0 };
    HANDLE in, out, err;
    DWORD code = (DWORD)-1;
    FILE* f;
    int i;
    char buffer[65536];
    size_t n;

    out = CreateFileA(outFile, GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    in = CreateFileA(c->input != NULL ? c->input : "NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    err = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, NULL);
    if (out == INVALID_HANDLE_VALUE || in == INVALID_HANDLE_VALUE || err == INVALID_HANDLE_VALUE) {
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
        if (err != INVALID_HANDLE_VALUE) CloseHandle(err);
        return -1;
    }

    add_quoted(&line, path);
    for (i = 0; i < c->argc; i++) {
        text_add(&line, " ", 1);
        add_quoted(&line, c->argv[i]);
    }

    // only the three handles are inherited: the other threads open files at the same time, and a
    // child that holds a handle of theirs would keep that file from being opened again
    inherited[0] = in;
    inherited[1] = out;
    inherited[2] = err;
    ZeroMemory(&si, sizeof si);
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = in;
    si.StartupInfo.hStdOutput = out;
    si.StartupInfo.hStdError = err;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attributeSize);
    si.lpAttributeList = malloc(attributeSize);
    InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attributeSize);
    UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited, NULL, NULL);
    if (CreateProcessA(path, line.text, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, env, NULL, &si.StartupInfo, &pi)) {
        CloseHandle(pi.hThread);
        if (WaitForSingleObject(pi.hProcess, TIMEOUT_MS) == WAIT_OBJECT_0) {
            GetExitCodeProcess(pi.hProcess, &code);
        } else {
            TerminateProcess(pi.hProcess, 255);
            WaitForSingleObject(pi.hProcess, INFINITE);
        }
        CloseHandle(pi.hProcess);
    }
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    CloseHandle(in);
    CloseHandle(out);
    CloseHandle(err);
    free(line.text);

    output->length = 0;
    f = fopen(outFile, "rb");
    if (f != NULL) {
        while ((n = fread(buffer, 1, sizeof buffer, f)) > 0)
            text_add(output, buffer, n);
        fclose(f);
    }
    return (int)code;
}

static void run_case(Case* c, const char* tempDir, int thread) {
    char gnuPath[MAX_PATH], ourPath[MAX_PATH], gnuOut[MAX_PATH], ourOut[MAX_PATH];
    static __declspec(thread) Text gnuText, ourText;

    snprintf(gnuPath, sizeof gnuPath, "%s\\%s.exe", gnuDir, c->tool);
    snprintf(ourPath, sizeof ourPath, "%s\\%s.exe", binDir, c->tool);
    snprintf(gnuOut, sizeof gnuOut, "%s\\g%d.out", tempDir, thread);
    snprintf(ourOut, sizeof ourOut, "%s\\o%d.out", tempDir, thread);

    c->expected = run_program(gnuPath, c, gnuOut, c->utf8 ? envUtf8 : envC, &gnuText);
    c->got = run_program(ourPath, c, ourOut, c->utf8 ? envUtf8 : envC, &ourText);
    c->differs = c->expected != c->got;
    if (!c->statusOnly && (gnuText.length != ourText.length || memcmp(gnuText.text, ourText.text, gnuText.length) != 0))
        c->differs = 1;
}

typedef struct {
    const char* tempDir;
    int index;
} Worker;

static DWORD WINAPI worker(LPVOID parameter) {
    Worker* w = parameter;
    for (;;) {
        LONG i = InterlockedIncrement(&nextCase) - 1;
        if (i >= caseCount)
            return 0;
        run_case(&cases[i], w->tempDir, w->index);
    }
}

/** The environment of this process with LC_ALL set, as a block for CreateProcess. */
static char* make_environment(const char* locale) {
    char* strings = GetEnvironmentStringsA();
    const char* p;
    size_t length = 0;
    char* block;
    size_t used = 0;

    for (p = strings; *p != '\0'; p += strlen(p) + 1)
        length += strlen(p) + 1;
    block = malloc(length + 64);
    for (p = strings; *p != '\0'; p += strlen(p) + 1) {
        size_t n = strlen(p) + 1;
        if (_strnicmp(p, "LC_ALL=", 7) == 0)
            continue;
        memcpy(block + used, p, n);
        used += n;
    }
    used += (size_t)sprintf(block + used, "LC_ALL=%s", locale) + 1;
    block[used] = '\0';
    FreeEnvironmentStringsA(strings);
    return block;
}

int main(int argc, char* argv[]) {
    SYSTEM_INFO system;
    const char* jobsText = getenv("WS_JOBS");
    char tempDir[MAX_PATH];
    HANDLE threads[64];
    Worker workers[64];
    int jobs, i, bad = 0;

    if (argc != 4) {
        fprintf(stderr, "usage: gnu_batch CASES GNU_DIR BIN_DIR\n");
        return 255;
    }
    gnuDir = argv[2];
    binDir = argv[3];
    if (!load(argv[1])) {
        fprintf(stderr, "gnu_batch: cannot read %s\n", argv[1]);
        return 255;
    }
    envC = make_environment("C");
    envUtf8 = make_environment("C.UTF-8");

    GetTempPathA(sizeof tempDir, tempDir);
    snprintf(tempDir + strlen(tempDir), sizeof tempDir - strlen(tempDir), "gnu_batch_%lu", (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(tempDir, NULL);

    GetSystemInfo(&system);
    jobs = jobsText != NULL ? atoi(jobsText) : (int)system.dwNumberOfProcessors;
    if (jobs < 1)
        jobs = 1;
    if (jobs > 64)
        jobs = 64;
    for (i = 0; i < jobs; i++) {
        workers[i].tempDir = tempDir;
        workers[i].index = i;
        threads[i] = CreateThread(NULL, 0, worker, &workers[i], 0, NULL);
    }
    WaitForMultipleObjects((DWORD)jobs, threads, TRUE, INFINITE);

    for (i = 0; i < caseCount; i++) {
        const Case* c = &cases[i];
        int k;
        if (!c->differs)
            continue;
        bad++;
        printf("DIFFERS: %s", c->tool);
        for (k = 0; k < c->argc; k++)
            printf(" %s", c->argv[k]);
        if (c->input != NULL)
            printf(" < %s", c->input);
        printf(" (status %d, ours %d)\n", c->expected, c->got);
    }

    for (i = 0; i < jobs; i++) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\g%d.out", tempDir, i);
        DeleteFileA(path);
        snprintf(path, sizeof path, "%s\\o%d.out", tempDir, i);
        DeleteFileA(path);
    }
    RemoveDirectoryA(tempDir);
    return bad > 255 ? 255 : bad;
}
