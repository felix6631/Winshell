/*
 * Tests of nohup, run as the program it is: the command runs with its arguments, its output and status come through
 * (standard output is not a terminal here, so nothing is redirected), and the statuses 125, 126 and 127. GNU nohup is
 * compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("nohup", arguments, input, expected, status);
}

static void message(const char* arguments, const char* part, int status) {
    RunResult r;

    run_tool(workDir, "nohup", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

int main(void) {
    tools_init("nohup_test");

    printf("running a command\n");
    expect("echo hi", "", "hi\n", 0);
    expect("echo a b c", "", "a b c\n", 0);
    expect("-- echo hi", "", "hi\n", 0);
    expect("echo -n x", "", "x", 0);                        // the options end at the command
    expect("cat", "x\ny\n", "x\ny\n", 0);                   // standard input is passed on
    expect("false", "", "", 1);                             // the status of the command
    expect("true", "", "", 0);

    printf("errors\n");
    message("", "missing operand", 125);
    message("nosuchcommand_zz", "failed to run command 'nosuchcommand_zz': No such file or directory", 127);
    message("--bogus", "unrecognized option", 125);
    expect_run("nohup", "--version", "", "nohup (Winshell) 0.1\n", 0);
    return test_summary("nohup_test");
}
