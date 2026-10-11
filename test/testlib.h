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

/** Full path of a built program: <repository>\bin\<name>.exe; test programs live in <repository>\test\out. */
static void bin_path(const char* name, char* out, size_t size) {
    char dir[MAX_PATH];
    test_exe_dir(dir, sizeof dir);
    snprintf(out, size, "%s\\..\\..\\bin\\%s.exe", dir, name);
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
    DWORD exitCode;
    int   timedOut;
} RunResult;

static void run_result_free(RunResult* result) {
    free(result->out);
    free(result->err);
    result->out = result->err = NULL;
}

static char* slurp(const char* path) {
    FILE* file = fopen(path, "rb");
    char* data;
    long size;
    if (file == NULL) {
        data = malloc(1);
        data[0] = '\0';
        return data;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = malloc((size_t)size + 1);
    size = (long)fread(data, 1, (size_t)size, file);
    data[size] = '\0';
    fclose(file);
    return data;
}

/**
 * Runs `"program" arguments` in directory cwd (NULL: the current one) with
 * input as standard input, and waits up to ten seconds.
 * Returns 0 when the program was started.
 */
static int run_program(const char* program, const char* arguments, const char* input,
                       const char* cwd, RunResult* result) {
    static int counter = 0;
    char temp[MAX_PATH], inPath[MAX_PATH + 64], outPath[MAX_PATH + 64], errPath[MAX_PATH + 64];
    char* commandLine;
    SECURITY_ATTRIBUTES sa;
    HANDLE hIn, hOut, hErr;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD wait;
    BOOL started;

    memset(result, 0, sizeof *result);

    GetTempPathA(sizeof temp, temp);
    counter++;
    snprintf(inPath,  sizeof inPath,  "%swinshell-test-%lu-%d.in",  temp, GetCurrentProcessId(), counter);
    snprintf(outPath, sizeof outPath, "%swinshell-test-%lu-%d.out", temp, GetCurrentProcessId(), counter);
    snprintf(errPath, sizeof errPath, "%swinshell-test-%lu-%d.err", temp, GetCurrentProcessId(), counter);

    {
        FILE* file = fopen(inPath, "wb");
        if (file == NULL)
            return -1;
        if (input != NULL)
            fwrite(input, 1, strlen(input), file);
        fclose(file);
    }

    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    hIn  = CreateFileA(inPath,  GENERIC_READ,  FILE_SHARE_READ, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    hOut = CreateFileA(outPath, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    hErr = CreateFileA(errPath, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    commandLine = malloc(strlen(program) + (arguments ? strlen(arguments) : 0) + 8);
    sprintf(commandLine, "\"%s\"%s%s", program, arguments && arguments[0] ? " " : "", arguments ? arguments : "");

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

    wait = WaitForSingleObject(pi.hProcess, 10000);
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

    result->out = slurp(outPath);
    result->err = slurp(errPath);
    DeleteFileA(inPath);
    DeleteFileA(outPath);
    DeleteFileA(errPath);
    return 0;
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
