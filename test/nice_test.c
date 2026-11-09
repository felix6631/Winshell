/*
 * Tests of nice, run as the program it is: the niceness printed without a command, a command run with an adjustment
 * (also the old forms -N and --N), and the statuses 125 and 127. GNU nice is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("nice", arguments, input, expected, status);
}

static void message(const char* arguments, const char* part, int status) {
    RunResult r;

    run_tool(workDir, "nice", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

int main(void) {
    tools_init("nice_test");

    printf("the niceness\n");
    expect("", "", "0\n", 0);
    expect("-n 12 nice", "", "10\n", 0);                   // below normal is 10: the new process has that class
    expect("-n 30 nice", "", "19\n", 0);                   // idle is 19 and the most there is
    expect("-n 3 nice", "", "0\n", 0);                     // a small adjustment stays the normal class
    expect("-n -10 nice", "", "-10\n", 0);                  // high priority (realtime needs the rights of an administrator)

    printf("running a command\n");
    expect("echo hi", "", "hi\n", 0);
    expect("-n 5 echo hi", "", "hi\n", 0);
    expect("-n5 echo hi", "", "hi\n", 0);
    expect("--adjustment=5 echo hi", "", "hi\n", 0);
    expect("--adjustment 5 echo hi", "", "hi\n", 0);
    expect("-5 echo hi", "", "hi\n", 0);                   // the old forms
    expect("--5 echo hi", "", "hi\n", 0);
    expect("-+5 echo hi", "", "hi\n", 0);
    expect("-n 5 -- echo hi", "", "hi\n", 0);
    expect("-n 100 echo big", "", "big\n", 0);             // clamped
    expect("echo -n x", "", "x", 0);                         // after the command the options are its own
    expect("cat", "in\n", "in\n", 0);
    expect("false", "", "", 1);

    printf("errors\n");
    message("-n 5", "a command must be given with an adjustment", 125);
    message("-n x echo", "invalid adjustment 'x'", 125);
    message("-n 1x echo", "invalid adjustment '1x'", 125);
    message("nosuchcommand_zz", "'nosuchcommand_zz': No such file or directory", 127);
    message("--bogus", "unrecognized option", 125);
    message("-n", "requires an argument", 125);
    expect_run("nice", "--version", "", "nice (Winshell) 0.1\n", 0);
    return test_summary("nice_test");
}
