/*
 * Tests of stat, run as the program it is: the format sequences, the default and terse outputs, the types of
 * files, times, -L on links and -f. GNU stat is compared with by gnu_compare.sh (the fields that mean the same).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* expected, int status) {
    expect_run("stat", arguments, "", expected, status);
}

int main(void) {
    RunResult r;
    char command[MAX_PATH * 3], expected[MAX_PATH * 2], path[MAX_PATH * 2];
    FILETIME ft;
    SYSTEMTIME t;
    HANDLE h;

    tools_init("stat_test");
    reset_dir(workDir);
    write_text(workDir, "a.txt", "hello");
    write_text(workDir, "empty.txt", "");
    make_directory("dir");

    // the modification time of a.txt: 2001-02-03 04:05:06 UTC (981173106)
    memset(&t, 0, sizeof t);
    t.wYear = 2001; t.wMonth = 2; t.wDay = 3; t.wHour = 4; t.wMinute = 5; t.wSecond = 6;
    SystemTimeToFileTime(&t, &ft);
    snprintf(path, sizeof path, "%s\\a.txt", workDir);
    h = CreateFileA(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    SetFileTime(h, NULL, &ft, &ft);
    CloseHandle(h);

    printf("format sequences\n");
    expect("-c %s a.txt", "5\n", 0);
    expect("-c %n a.txt", "a.txt\n", 0);
    expect("-c %N a.txt", "'a.txt'\n", 0);
    expect("-c %F a.txt", "regular file\n", 0);
    expect("-c %F empty.txt", "regular empty file\n", 0);
    expect("-c %F dir", "directory\n", 0);
    expect("-c %h a.txt", "1\n", 0);
    expect("-c %a a.txt", "644\n", 0);
    expect("-c %A a.txt", "-rw-r--r--\n", 0);
    expect("-c %a dir", "755\n", 0);
    expect("-c %A dir", "drwxr-xr-x\n", 0);
    expect("-c %f a.txt", "81a4\n", 0);
    expect("-c %Y a.txt", "981173106\n", 0);
    expect("-c %X a.txt", "981173106\n", 0);
    expect("-c %s dir", "0\n", 0);
    expect("-c [%5s][%-5s][%05s] a.txt", "[    5][5    ][00005]\n", 0);
    expect("-c 100%% a.txt", "100%\n", 0);
    expect("-c %n:%s a.txt empty.txt", "a.txt:5\nempty.txt:0\n", 0);
    expect("--printf=%n\\t%s\\n a.txt", "a.txt\t5\n", 0);
    expect("--printf=%n a.txt", "a.txt", 0);                                // no line feed added
    expect("--printf=\\x41\\101 a.txt", "AA", 0);
    expect("-c %B a.txt", "512\n", 0);

    printf("times\n");
    run_tool(workDir, "stat", "-c %y a.txt", "", &r);
    CHECK(r.out != NULL && strlen(r.out) > 30 && r.out[4] == '-' && strstr(r.out, ".000000000 ") != NULL);
    run_result_free(&r);
    run_tool(workDir, "stat", "-c %w|%W a.txt", "", &r);
    CHECK(r.out != NULL && strchr(r.out, '|') != NULL && r.out[0] == '2');
    run_result_free(&r);

    printf("default and terse\n");
    run_tool(workDir, "stat", "a.txt", "", &r);
    CHECK(r.out != NULL && strncmp(r.out, "  File: a.txt\n  Size: 5", 23) == 0);
    CHECK(strstr(r.out, "regular file") != NULL && strstr(r.out, "Links: 1") != NULL);
    CHECK(strstr(r.out, "Access: (0644/-rw-r--r--)") != NULL);
    CHECK(strstr(r.out, "Modify: 2001-") != NULL && strstr(r.out, " Birth: ") != NULL);
    run_result_free(&r);
    run_tool(workDir, "stat", "-t a.txt", "", &r);
    CHECK(r.out != NULL && strncmp(r.out, "a.txt 5 ", 8) == 0);
    run_result_free(&r);
    run_tool(workDir, "stat", "dir", "", &r);
    CHECK(r.out != NULL && strstr(r.out, "directory") != NULL && strstr(r.out, "drwxr-xr-x") != NULL);
    run_result_free(&r);

    printf("links\n");
    snprintf(command, sizeof command, "mklink /J \"%s\\link\" \"%s\\dir\" > nul", workDir, workDir);
    if (system(command) != 0) {
        printf("(skipped: no junction could be made)\n");
    } else {
        snprintf(expected, sizeof expected, "'link' -> '%s/dir'\n", virtualWorkDir);
        expect("-c %N link", expected, 0);
        expect("-c %F link", "symbolic link\n", 0);
        expect("-L -c %F link", "directory\n", 0);
    }

    printf("file systems\n");
    run_tool(workDir, "stat", "-f a.txt", "", &r);
    CHECK(r.out != NULL && strstr(r.out, "Block size:") != NULL && strstr(r.out, "Blocks: Total:") != NULL);
    run_result_free(&r);
    run_tool(workDir, "stat", "-f -c %T a.txt", "", &r);
    CHECK(r.out != NULL && r.exitCode == 0 && strlen(r.out) > 1);
    run_result_free(&r);

    printf("errors\n");
    expect("nosuchfile", "", 1);
    expect("-c %s a.txt nosuchfile empty.txt", "5\n0\n", 1);
    expect("", "", 1);
    expect("-x", "", 1);
    expect("--version", "stat (Winshell) 0.1\n", 0);

    return test_summary("stat_test");
}
