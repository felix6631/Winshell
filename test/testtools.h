#ifndef TESTTOOLS_H
#define TESTTOOLS_H

/*
 * Helpers of the tests that run the programs of the project: the work directory, running a
 * tool in it, creating fixtures and checking their results, and running a script in the shell.
 * Each test program includes this file and calls tools_init() first.
 */

#include <errno.h>
#include "testlib.h"

static char workDir[MAX_PATH];
static char virtualWorkDir[MAX_PATH];
static char shellPath[MAX_PATH * 2];

/** Makes the work directory <test>\work\<name> and finds the shell. */
static void tools_init(const char* name) {
    make_work_dir(name, workDir, sizeof workDir);
    expected_virtual(workDir, virtualWorkDir, sizeof virtualWorkDir);
    bin_path("shell", shellPath, sizeof shellPath);
}

/** Runs bin\<name>.exe; the output is only valid until run_result_free. */
static int run_command(const char* name, const char* arguments, const char* input, RunResult* result) {
    char path[MAX_PATH * 2];
    int status;

    bin_path(name, path, sizeof path);
    status = run_program(path, arguments, input, workDir, result);
    CHECK(status == 0);
    CHECK(!result->timedOut);
    return status;
}

/* ---- rm, cp, mv ------------------------------------------------------------- */
/** Runs bin\<name>.exe in the given directory. */
static void run_tool(const char* cwd, const char* name, const char* arguments, const char* input, RunResult* r) {
    char path[MAX_PATH * 2];
    bin_path(name, path, sizeof path);
    CHECK(run_program(path, arguments, input, cwd, r) == 0);
    CHECK(!r->timedOut);
}

/* ---- head, tail, wc -------------------------------------------------------------- */
/**
 * Runs bin\<tool>.exe in the work directory with the arguments and the standard input, and checks the bytes it
 * wrote to standard output and its exit status. A difference shows the tool, its arguments and what it wrote.
 */
static void expect_run(const char* tool, const char* arguments, const char* input, const char* expected, int status) {
    RunResult r;

    run_tool(workDir, tool, arguments, input, &r);
    if (r.out == NULL || strcmp(r.out, expected) != 0)
        printf("    for: %s %s\n    got: \"%s\"\n", tool, arguments, r.out == NULL ? "(null)" : r.out);
    CHECK_STR(r.out, expected);
    CHECK_INT(r.exitCode, status);
    run_result_free(&r);
}

/** The same for a tool that has to succeed. */
static void text_expect(const char* tool, const char* arguments, const char* input, const char* expected) {
    expect_run(tool, arguments, input, expected, 0);
}

/** Writes a file of the given size, clearing attributes an earlier run may have set. */
static void make_sized_file(const char* directory, const char* name, size_t size, DWORD attributes) {
    char path[MAX_PATH * 2];
    char* data = malloc(size + 1);

    snprintf(path, sizeof path, "%s\\%s", directory, name);
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    memset(data, 'x', size);
    write_file(directory, name, data, size);
    if (attributes != FILE_ATTRIBUTE_NORMAL)
        SetFileAttributesA(path, attributes);
    free(data);
}

static void set_mtime(const char* directory, const char* name, int year, int month, int day) {
    char path[MAX_PATH * 2];
    SYSTEMTIME t;
    FILETIME ft;
    HANDLE h;

    snprintf(path, sizeof path, "%s\\%s", directory, name);
    memset(&t, 0, sizeof t);
    t.wYear = (WORD)year;
    t.wMonth = (WORD)month;
    t.wDay = (WORD)day;
    t.wHour = 12;
    SystemTimeToFileTime(&t, &ft);
    h = CreateFileA(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SetFileTime(h, NULL, NULL, &ft);
        CloseHandle(h);
    }
}

static int has_line(const char* text, const char* line) {
    size_t length = strlen(line);
    const char* p = text;
    while ((p = strstr(p, line)) != NULL) {
        if ((p == text || p[-1] == '\n') && p[length] == '\n')
            return 1;
        p += length;
    }
    return 0;
}

/** Makes the directory empty (and read-write) again; done with the system tools, not with those under test. */
static void reset_dir(const char* path) {
    char arguments[MAX_PATH * 4];
    const char* comspec = getenv("ComSpec");
    RunResult r;

    snprintf(arguments, sizeof arguments,
             "/c attrib -r \"%s\\*\" /s /d >nul 2>&1 & rd /s /q \"%s\" >nul 2>&1 & md \"%s\"", path, path, path);
    run_program(comspec != NULL ? comspec : "cmd.exe", arguments, NULL, NULL, &r);
    run_result_free(&r);
}

static int path_exists_in(const char* directory, const char* name) {
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\%s", directory, name);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static DWORD attributes_in(const char* directory, const char* name) {
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\%s", directory, name);
    return GetFileAttributesA(path);
}

static void make_dir_in(const char* directory, const char* name) {
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\%s", directory, name);
    CreateDirectoryA(path, NULL);
}

static int year_of(const char* directory, const char* name) {
    char path[MAX_PATH * 2];
    HANDLE h;
    FILETIME ft;
    SYSTEMTIME t;
    int year = 0;

    snprintf(path, sizeof path, "%s\\%s", directory, name);
    h = CreateFileA(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        if (GetFileTime(h, NULL, NULL, &ft) && FileTimeToSystemTime(&ft, &t))
            year = t.wYear;
        CloseHandle(h);
    }
    return year;
}

/** The content of a file must be exactly the given text. */
static void expect_content(const char* directory, const char* name, const char* expected) {
    char* text = read_file(directory, name, NULL);
    if (text == NULL) {
        g_checks++;
        g_failures++;
        printf("  FAIL missing file %s\\%s\n", directory, name);
        return;
    }
    CHECK_STR(text, expected);
    free(text);
}

/* ---- mkdir, rmdir, touch ------------------------------------------------------ */
/** Reads one of the three times of a file or directory (UTC). which: 0 created, 1 accessed, 2 written. */
static int time_of(const char* directory, const char* name, int which, SYSTEMTIME* out) {
    char path[MAX_PATH * 2];
    HANDLE h;
    FILETIME created, accessed, written;
    int ok = 0;

    snprintf(path, sizeof path, "%s\\%s", directory, name);
    h = CreateFileA(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        if (GetFileTime(h, &created, &accessed, &written)) {
            FILETIME* chosen = which == 0 ? &created : (which == 1 ? &accessed : &written);
            ok = FileTimeToSystemTime(chosen, out) != 0;
        }
        CloseHandle(h);
    }
    return ok;
}

static void expect_time(const char* directory, const char* name, int which,
                        int year, int month, int day, int hour, int minute, int second) {
    SYSTEMTIME t;
    g_checks++;
    if (!time_of(directory, name, which, &t) || t.wYear != year || t.wMonth != month || t.wDay != day ||
        t.wHour != hour || t.wMinute != minute || t.wSecond != second) {
        g_failures++;
        if (time_of(directory, name, which, &t))
            printf("  FAIL %s %s: time is %04d-%02d-%02d %02d:%02d:%02d, expected %04d-%02d-%02d %02d:%02d:%02d\n",
                   directory, name, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                   year, month, day, hour, minute, second);
        else
            printf("  FAIL %s %s: no such file\n", directory, name);
    }
}

/**
 * Runs the lines as a script. The shell prints its prompt before each line;
 * the prompts and CRs are removed from the output so that it can be compared.
 */
static void script(const char* lines, RunResult* r) {
    int status = run_program(shellPath, "", lines, workDir, r);
    CHECK(status == 0);
    CHECK(!r->timedOut);
    if (r->out != NULL)
        without_prompts(lf_only(r->out));
    if (r->err != NULL)
        lf_only(r->err);
}

#define SCRIPT_OUT(lines, expected) \
    do { RunResult r_; script(lines, &r_); CHECK_STR(r_.out, expected); run_result_free(&r_); } while (0)

static void make_directory(const char* relative) {
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\%s", workDir, relative);
    CreateDirectoryA(path, NULL);
}

#endif //TESTTOOLS_H
