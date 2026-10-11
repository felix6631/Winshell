#ifndef TESTLIB_H
#define TESTLIB_H

/*
 * A very small test library, shared by the programs in this directory.
 *
 *  - CHECK macros count and print failures; test_summary() gives the exit code.
 *  - run_program() starts a program with a given standard input and collects
 *    standard output, standard error and the exit code.
 *  - The work directory is a fresh directory below the test program, for
 *    fixtures and as the current directory of the programs under test.
 *
 * Everything here is written against plain Win32, so the tests run wherever
 * the programs do.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int g_checks = 0;
static int g_failures = 0;

static void check_fail(const char* file, int line, const char* what) {
    g_failures++;
    printf("  FAIL %s:%d: %s\n", file, line, what);
}

#define CHECK(condition) \
    do { g_checks++; if (!(condition)) check_fail(__FILE__, __LINE__, #condition); } while (0)

static void check_str(const char* file, int line, const char* expression, const char* actual, const char* expected) {
    g_checks++;
    if (actual == NULL || strcmp(actual, expected) != 0) {
        g_failures++;
        printf("  FAIL %s:%d: %s\n    expected: \"%s\"\n    actual:   \"%s\"\n",
               file, line, expression, expected, actual == NULL ? "(null)" : actual);
    }
}

#define CHECK_STR(actual, expected) check_str(__FILE__, __LINE__, #actual, (actual), (expected))

static void check_int(const char* file, int line, const char* expression, long actual, long expected) {
    g_checks++;
    if (actual != expected) {
        g_failures++;
        printf("  FAIL %s:%d: %s\n    expected: %ld\n    actual:   %ld\n", file, line, expression, expected, actual);
    }
}

#define CHECK_INT(actual, expected) check_int(__FILE__, __LINE__, #actual, (long)(actual), (long)(expected))

/** Prints the result of the program and returns the exit code to use. */
static int test_summary(const char* name) {
    printf("%s: %d checks, %d failed\n", name, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

/* ---- paths -------------------------------------------------------------- */

/** Directory of the running test program, without a trailing backslash. */
static void test_exe_dir(char* out, size_t size) {
    char* separator;
    GetModuleFileNameA(NULL, out, (DWORD)size);
    separator = strrchr(out, '\\');
    if (separator != NULL)
        *separator = '\0';
}

/**
 * Full path of a built program: <repository>\bin\<name>.exe; test programs live in
 * <repository>\test\out. The environment variable WINSHELL_BIN names another directory of
 * programs, which is how the suite runs against the build with AddressSanitizer.
 */
static void bin_path(const char* name, char* out, size_t size) {
    const char* override = getenv("WINSHELL_BIN");
    if (override != NULL && override[0] != '\0') {
        snprintf(out, size, "%s\\%s.exe", override, name);
        return;
    } else {
        char dir[MAX_PATH];
        test_exe_dir(dir, sizeof dir);
        snprintf(out, size, "%s\\..\\..\\bin\\%s.exe", dir, name);
    }
}

/**
 * Creates <test program directory>\work\<name> if needed and returns its path.
 * Fixtures are always written before they are used, so old files do no harm.
 */
static void make_work_dir(const char* name, char* out, size_t size) {
    char dir[MAX_PATH], work[MAX_PATH + 8];
    test_exe_dir(dir, sizeof dir);
    snprintf(work, sizeof work, "%s\\work", dir);
    snprintf(out, size, "%s\\%s", work, name);
    CreateDirectoryA(work, NULL);
    CreateDirectoryA(out, NULL);
}

static int write_file(const char* directory, const char* name, const void* data, size_t length) {
    char path[MAX_PATH * 2];
    FILE* file;
    snprintf(path, sizeof path, "%s\\%s", directory, name);
    file = fopen(path, "wb");
    if (file == NULL)
        return -1;
    fwrite(data, 1, length, file);
    fclose(file);
    return 0;
}

static int write_text(const char* directory, const char* name, const char* text) {
    return write_file(directory, name, text, strlen(text));
}

/** Reads a whole file. The result has to be freed; *length is optional. */
static char* read_file(const char* directory, const char* name, size_t* length) {
    char path[MAX_PATH * 2];
    FILE* file;
    char* data;
    long size;

    snprintf(path, sizeof path, "%s\\%s", directory, name);
    file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = malloc((size_t)size + 1);
    if (data != NULL) {
        size = (long)fread(data, 1, (size_t)size, file);
        data[size] = '\0';
        if (length != NULL)
            *length = (size_t)size;
    }
    fclose(file);
    return data;
}

/**
 * The virtual path of a Windows path ("C:\a\b" -> "/c/a/b"), worked out here
 * independently of the code under test.
 */
static void expected_virtual(const char* native, char* out, size_t size) {
    size_t i, length = strlen(native);
    if (length < 2 || native[1] != ':' || length + 1 > size) {
        out[0] = '\0';
        return;
    }
    out[0] = '/';
    out[1] = (char)((native[0] >= 'A' && native[0] <= 'Z') ? native[0] + 32 : native[0]);
    for (i = 2; i < length; i++)
        out[i] = native[i] == '\\' ? '/' : native[i];
    // "C:\" is "/c", not "/c/"
    if (i > 2 && out[i - 1] == '/')
        i--;
    out[i] = '\0';
}

/* ---- running programs ----------------------------------------------------- */

typedef struct {
    char* out;          // standard output
    char* err;          // standard error
    size_t outLength, errLength;
    DWORD exitCode;
    int   timedOut;
} RunResult;

static void run_result_free(RunResult* result) {
    free(result->out);
    free(result->err);
    result->out = result->err = NULL;
}

/** Reads a whole file, which may hold NUL bytes. The result has to be freed and always ends with a NUL. */
static char* slurp_n(const char* path, size_t* length) {
    FILE* file = fopen(path, "rb");
    char* data;
    long long size;
    size_t got;

    if (file == NULL) {
        data = malloc(1);
        data[0] = '\0';
        *length = 0;
        return data;
    }
    _fseeki64(file, 0, SEEK_END);
    size = _ftelli64(file);
    _fseeki64(file, 0, SEEK_SET);
    data = malloc((size_t)size + 1);
    got = fread(data, 1, (size_t)size, file);
    data[got] = '\0';
    *length = got;
    fclose(file);
    return data;
}

static char* slurp(const char* path) {
    size_t length;
    return slurp_n(path, &length);
}

static void temp_path(const char* suffix, char* out, size_t size) {
    static int counter = 0;
    char temp[MAX_PATH];
    GetTempPathA(sizeof temp, temp);
    snprintf(out, size, "%swinshell-test-%lu-%d.%s", temp, GetCurrentProcessId(), ++counter, suffix);
}

/** Opens a file so that a child can inherit the handle. */
static HANDLE open_inheritable(const char* path, DWORD access, DWORD share, DWORD disposition) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    return CreateFileA(path, access, share, &sa, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
}

static char* command_line(const char* program, const char* arguments) {
    char* line = malloc(strlen(program) + (arguments ? strlen(arguments) : 0) + 8);
    sprintf(line, "\"%s\"%s%s", program, arguments && arguments[0] ? " " : "", arguments ? arguments : "");
    return line;
}

/**
 * Runs `"program" arguments` in directory cwd (NULL: the current one) and waits up to timeoutMs.
 * Standard input is inputLength bytes of input, or the file inputPath when that is not NULL.
 * Returns 0 when the program was started.
 */
static int run_program_ex(const char* program, const char* arguments, const char* input, size_t inputLength,
                          const char* inputPath, const char* cwd, DWORD timeoutMs, RunResult* result) {
    char inPath[MAX_PATH + 64], outPath[MAX_PATH + 64], errPath[MAX_PATH + 64];
    char* commandLine;
    HANDLE hIn, hOut, hErr;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD wait;
    BOOL started;
    int madeInput = 0;

    memset(result, 0, sizeof *result);

    temp_path("out", outPath, sizeof outPath);
    temp_path("err", errPath, sizeof errPath);

    if (inputPath != NULL) {
        snprintf(inPath, sizeof inPath, "%s", inputPath);
    } else {
        FILE* file;
        temp_path("in", inPath, sizeof inPath);
        file = fopen(inPath, "wb");
        if (file == NULL)
            return -1;
        if (input != NULL && inputLength > 0)
            fwrite(input, 1, inputLength, file);
        fclose(file);
        madeInput = 1;
    }

    hIn  = open_inheritable(inPath,  GENERIC_READ,  FILE_SHARE_READ, OPEN_EXISTING);
    hOut = open_inheritable(outPath, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS);
    hErr = open_inheritable(errPath, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS);

    commandLine = command_line(program, arguments);

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hIn;
    si.hStdOutput = hOut;
    si.hStdError = hErr;
    ZeroMemory(&pi, sizeof pi);

    started = CreateProcessA(NULL, commandLine, NULL, NULL, TRUE, 0, NULL, cwd, &si, &pi);
    free(commandLine);

    if (!started) {
        CloseHandle(hIn);
        CloseHandle(hOut);
        CloseHandle(hErr);
        return -1;
    }

    wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        result->timedOut = 1;
    }
    GetExitCodeProcess(pi.hProcess, &result->exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hIn);
    CloseHandle(hOut);
    CloseHandle(hErr);

    result->out = slurp_n(outPath, &result->outLength);
    result->err = slurp_n(errPath, &result->errLength);
    if (madeInput)
        DeleteFileA(inPath);
    DeleteFileA(outPath);
    DeleteFileA(errPath);
    return 0;
}

/** Runs `"program" arguments` in cwd with input as standard input, and waits up to ten seconds. */
static int run_program(const char* program, const char* arguments, const char* input,
                       const char* cwd, RunResult* result) {
    return run_program_ex(program, arguments, input, input != NULL ? strlen(input) : 0, NULL, cwd, 10000, result);
}

/* ---- programs that run on while the test goes on ------------------------------ */

/*
 * run_program waits for the program. A Process is one that has been started
 * and goes on: the test writes to its standard input (a pipe), looks at what
 * it has written so far, and ends it, or waits for it to end.
 */
typedef struct {
    HANDLE process;
    HANDLE inputWrite;              // our end of the pipe to standard input
    char   outPath[MAX_PATH + 64];
    char   errPath[MAX_PATH + 64];
    int    started;
} Process;

/** Starts the program with a pipe as standard input. Returns 0 when it was started. */
static int proc_start(Process* p, const char* program, const char* arguments, const char* cwd) {
    SECURITY_ATTRIBUTES sa;
    HANDLE inputRead = NULL, hOut, hErr;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char* commandLine;
    BOOL started;

    memset(p, 0, sizeof *p);
    temp_path("out", p->outPath, sizeof p->outPath);
    temp_path("err", p->errPath, sizeof p->errPath);

    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&inputRead, &p->inputWrite, &sa, 0))
        return -1;
    // the child must not hold the write end, or it would never see the end of its input
    SetHandleInformation(p->inputWrite, HANDLE_FLAG_INHERIT, 0);

    hOut = open_inheritable(p->outPath, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS);
    hErr = open_inheritable(p->errPath, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS);

    commandLine = command_line(program, arguments);
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inputRead;
    si.hStdOutput = hOut;
    si.hStdError = hErr;
    ZeroMemory(&pi, sizeof pi);

    started = CreateProcessA(NULL, commandLine, NULL, NULL, TRUE, 0, NULL, cwd, &si, &pi);
    free(commandLine);
    CloseHandle(inputRead);
    CloseHandle(hOut);
    CloseHandle(hErr);

    if (!started) {
        CloseHandle(p->inputWrite);
        p->inputWrite = NULL;
        return -1;
    }
    CloseHandle(pi.hThread);
    p->process = pi.hProcess;
    p->started = 1;
    return 0;
}

/** Writes to the standard input of the program. Returns 0 on success. */
static int proc_write(Process* p, const void* data, size_t length) {
    DWORD written;
    if (p->inputWrite == NULL)
        return -1;
    return WriteFile(p->inputWrite, data, (DWORD)length, &written, NULL) && written == length ? 0 : -1;
}

/** Closes the standard input: the program sees the end of its input. */
static void proc_close_input(Process* p) {
    if (p->inputWrite != NULL)
        CloseHandle(p->inputWrite);
    p->inputWrite = NULL;
}

/** Non-zero when the program has ended. */
static int proc_finished(Process* p) {
    return !p->started || WaitForSingleObject(p->process, 0) == WAIT_OBJECT_0;
}

/** What the program has written to standard output so far. The result has to be freed. */
static char* proc_output(Process* p, size_t* length) {
    size_t dummy;
    return slurp_n(p->outPath, length != NULL ? length : &dummy);
}

/** Waits until the output contains the text; returns 1 when it does, 0 after the time ran out. */
static int proc_wait_for_output(Process* p, const char* needle, DWORD timeoutMs) {
    DWORD waited = 0;
    for (;;) {
        char* text = proc_output(p, NULL);
        int found = strstr(text, needle) != NULL;
        free(text);
        if (found)
            return 1;
        if (waited >= timeoutMs)
            return 0;
        Sleep(20);
        waited += 20;
    }
}

static void proc_collect(Process* p, RunResult* result) {
    result->out = slurp_n(p->outPath, &result->outLength);
    result->err = slurp_n(p->errPath, &result->errLength);
    DeleteFileA(p->outPath);
    DeleteFileA(p->errPath);
    CloseHandle(p->process);
    p->process = NULL;
    p->started = 0;
}

/**
 * Waits for the program to end and gets its output and exit code.
 * Returns 0 when it ended, -1 when it was still running after timeoutMs (nothing is collected then).
 */
static int proc_wait(Process* p, DWORD timeoutMs, RunResult* result) {
    memset(result, 0, sizeof *result);
    if (WaitForSingleObject(p->process, timeoutMs) != WAIT_OBJECT_0)
        return -1;
    GetExitCodeProcess(p->process, &result->exitCode);
    proc_close_input(p);
    proc_collect(p, result);
    return 0;
}

/** Ends the program and gets what it had written. */
static void proc_kill(Process* p, RunResult* result) {
    memset(result, 0, sizeof *result);
    if (p->started && WaitForSingleObject(p->process, 0) != WAIT_OBJECT_0) {
        TerminateProcess(p->process, 1);
        WaitForSingleObject(p->process, 2000);
    }
    if (p->started)
        GetExitCodeProcess(p->process, &result->exitCode);
    proc_close_input(p);
    if (p->started)
        proc_collect(p, result);
}


/* ---- comparing output -------------------------------------------------- */

/** Removes every CR, so that output can be compared whatever line endings the program wrote. */
static char* lf_only(char* text) {
    char *read = text, *write = text;
    for (; *read != '\0'; read++)
        if (*read != '\r')
            *write++ = *read;
    *write = '\0';
    return text;
}

/** Removes every " > " the shell printed as prompt. */
static char* without_prompts(char* text) {
    char *read = text, *write = text;
    while (*read != '\0') {
        if (strncmp(read, " > ", 3) == 0) {
            read += 3;
            continue;
        }
        *write++ = *read++;
    }
    *write = '\0';
    return text;
}

#endif //TESTLIB_H
