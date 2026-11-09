/*
 * Tests of hostid, run as the program it is: eight hexadecimal digits that stay the same. GNU hostid is compared
 * with by gnu_compare.sh (errors and exit codes).
 */

#include "testtools.h"

int main(void) {
    RunResult r, again;
    size_t i;

    tools_init("hostid_test");

    printf("hostid\n");
    run_tool(workDir, "hostid", "", "", &r);
    run_tool(workDir, "hostid", "", "", &again);
    CHECK_INT(r.exitCode, 0);
    CHECK(r.out != NULL && strlen(r.out) == 9 && r.out[8] == '\n');
    for (i = 0; r.out != NULL && i < 8; i++)
        CHECK((r.out[i] >= '0' && r.out[i] <= '9') || (r.out[i] >= 'a' && r.out[i] <= 'f'));
    CHECK_STR(r.out, again.out);                     // it is the same each time
    run_result_free(&r);
    run_result_free(&again);

    expect_run("hostid", "extra", "", "", 1);
    expect_run("hostid", "--bogus", "", "", 1);
    expect_run("hostid", "--version", "", "hostid (Winshell) 0.1\n", 0);
    return test_summary("hostid_test");
}
