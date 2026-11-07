/*
 * Tests of whoami, run as the program it is. GNU whoami is compared with by gnu_compare.sh.
 */

#include "testtools.h"

int main(void) {
    RunResult r;
    char expected[300];
    const char* user = getenv("USERNAME");

    tools_init("whoami_test");

    printf("whoami\n");
    run_tool(workDir, "whoami", "", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(r.out != NULL && strlen(r.out) > 1 && r.out[strlen(r.out) - 1] == '\n' && strchr(r.out, '\r') == NULL);
    if (user != NULL) {
        snprintf(expected, sizeof expected, "%s\n", user);
        CHECK_STR(r.out, expected);        // the name of the account, as Windows has it in USERNAME
    }
    run_result_free(&r);

    expect_run("whoami", "extra", "", "", 1);
    expect_run("whoami", "--version", "", "whoami (Winshell) 0.1\n", 0);
    return test_summary("whoami_test");
}
