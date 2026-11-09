/*
 * Tests of users, run as the program it is: a line of names or nothing, and the errors. The sessions are those of the
 * system, so the output is checked for its shape. GNU users is compared with by gnu_compare.sh (errors and exit codes).
 */

#include "testtools.h"

int main(void) {
    RunResult r;

    tools_init("users_test");

    printf("users\n");
    run_tool(workDir, "users", "", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(r.out != NULL && (r.out[0] == '\0' || (r.out[strlen(r.out) - 1] == '\n' && strchr(r.out, '\r') == NULL)));
    CHECK(r.out == NULL || strchr(r.out, '\n') == NULL || strchr(r.out, '\n') == r.out + strlen(r.out) - 1);   // one line
    run_result_free(&r);

    run_tool(workDir, "users", "x", "", &r);                 // a FILE operand is accepted
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    expect_run("users", "x y", "", "", 1);
    expect_run("users", "--bogus", "", "", 1);
    expect_run("users", "--version", "", "users (Winshell) 0.1\n", 0);
    return test_summary("users_test");
}
