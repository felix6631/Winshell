/*
 * Tests of uname, run as the program it is. GNU uname is compared with by gnu_compare.sh (the machine and the
 * node name; the system name and the release of Windows are its own).
 */

#include "testtools.h"

static int word_count(const char* text) {
    int count = 0, in = 0;

    for (; *text != '\0'; text++) {
        if (*text == ' ' || *text == '\n') {
            in = 0;
        } else if (!in) {
            in = 1;
            count++;
        }
    }
    return count;
}

int main(void) {
    RunResult r;

    tools_init("uname_test");

    printf("uname\n");
    expect_run("uname", "", "", "Windows_NT\n", 0);
    expect_run("uname", "-s", "", "Windows_NT\n", 0);
    expect_run("uname", "-o", "", "Windows\n", 0);
    expect_run("uname", "-p", "", "unknown\n", 0);
    expect_run("uname", "-i", "", "unknown\n", 0);

    run_tool(workDir, "uname", "-r", "", &r);
    CHECK(r.out != NULL && strchr(r.out, '.') != NULL && r.out[0] >= '0' && r.out[0] <= '9');   // 10.0.26200
    run_result_free(&r);
    run_tool(workDir, "uname", "-v", "", &r);
    CHECK(r.out != NULL && r.out[0] == '#');
    run_result_free(&r);
    run_tool(workDir, "uname", "-m", "", &r);
    CHECK(r.out != NULL && (strcmp(r.out, "x86_64\n") == 0 || strcmp(r.out, "i686\n") == 0 || strcmp(r.out, "aarch64\n") == 0));
    run_result_free(&r);

    // -a: system node release version machine os; the options in the order of uname, whatever the order given
    run_tool(workDir, "uname", "-a", "", &r);
    CHECK(r.out != NULL && word_count(r.out) == 6 && strncmp(r.out, "Windows_NT ", 11) == 0);
    CHECK(strstr(r.out, " Windows\n") != NULL);
    run_result_free(&r);
    run_tool(workDir, "uname", "-ms", "", &r);
    CHECK(r.out != NULL && strncmp(r.out, "Windows_NT ", 11) == 0 && word_count(r.out) == 2);
    run_result_free(&r);

    expect_run("uname", "extra", "", "", 1);
    expect_run("uname", "-x", "", "", 1);
    expect_run("uname", "--version", "", "uname (Winshell) 0.1\n", 0);
    return test_summary("uname_test");
}
