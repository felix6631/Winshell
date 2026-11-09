/*
 * Tests of logname, run as the program it is. GNU logname is compared with by gnu_compare.sh.
 */

#include "testtools.h"

int main(void) {
    RunResult r, who;

    tools_init("logname_test");

    printf("logname\n");
    run_tool(workDir, "logname", "", "", &r);
    run_tool(workDir, "whoami", "", "", &who);
    CHECK_INT(r.exitCode, 0);
    CHECK(r.out != NULL && strlen(r.out) > 1 && r.out[strlen(r.out) - 1] == '\n' && strchr(r.out, '\r') == NULL);
    CHECK_STR(r.out, who.out);                      // the account is the login
    run_result_free(&r);
    run_result_free(&who);

    expect_run("logname", "extra", "", "", 1);
    expect_run("logname", "--bogus", "", "", 1);
    expect_run("logname", "--version", "", "logname (Winshell) 0.1\n", 0);
    return test_summary("logname_test");
}
