/*
 * Tests of timeout, run as the program it is: a command that ends in time keeps its output and status, one that does not is
 * ended with the status 124 (or 137 when it has to be killed, or 128 + the signal with --preserve-status), the durations and
 * the signals are checked as GNU timeout checks them, and the status is 125 for an error of timeout, 126 and 127 for a
 * command that cannot be run. GNU timeout is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("timeout", arguments, input, expected, status);
}

static void message(const char* arguments, const char* expectedError, int status) {
    RunResult r;

    run_tool(workDir, "timeout", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    lf_only(r.err);
    if (strcmp(r.err, expectedError) != 0)
        printf("    for: timeout %s\n    got: \"%s\"\n", arguments, r.err);
    CHECK_STR(r.err, expectedError);
    run_result_free(&r);
}

int main(void) {
    const char* usage = "Try 'timeout --help' for more information.\n";
    char text[256];

    tools_init("timeout_test");
    reset_dir(workDir);

    printf("a command that ends in time\n");
    expect("5 echo hi", "", "hi\n", 0);
    expect("5s echo hi", "", "hi\n", 0);
    expect("0.5m echo hi", "", "hi\n", 0);
    expect("1e1 echo hi", "", "hi\n", 0);
    expect(".5 echo hi", "", "hi\n", 0);
    expect("0 echo hi", "", "hi\n", 0);                                       // 0: no time limit
    expect("5 cat", "in\n", "in\n", 0);                                        // standard input and output reach the command
    expect("5 false", "", "", 1);                                              // the status of the command
    expect("5 echo -n x", "", "x", 0);                                         // after the duration the options are the command's
    expect("-- 5 echo hi", "", "hi\n", 0);
    expect("--foreground 5 echo hi", "", "hi\n", 0);
    expect("-v 5 echo hi", "", "hi\n", 0);

    printf("a command that times out\n");
    expect("0.3 sleep 20", "", "", 124);
    expect("0.3s sleep 20", "", "", 124);
    expect("--foreground 0.3 sleep 20", "", "", 124);
    expect("--preserve-status 0.3 sleep 20", "", "", 143);                    // 128 + TERM
    expect("-s INT --preserve-status 0.3 sleep 20", "", "", 130);
    expect("-s SIGHUP 0.3 sleep 20", "", "", 124);
    expect("-s 15 0.3 sleep 20", "", "", 124);
    expect("-s KILL 0.3 sleep 20", "", "", 137);
    expect("-s 9 0.3 sleep 20", "", "", 137);
    expect("-s CHLD -k 0.3 0.3 sleep 20", "", "", 137);                       // a signal that does nothing, then the KILL after
    message("-v 0.3 sleep 20", "timeout: sending signal TERM to command 'sleep'\n", 124);
    message("-v -s KILL 0.3 sleep 20", "timeout: sending signal KILL to command 'sleep'\n", 137);
    message("-v -s USR1 -k 0.3 0.3 sleep 20",
            "timeout: sending signal USR1 to command 'sleep'\n", 124);

    printf("the durations and the signals\n");
    snprintf(text, sizeof text, "%s", usage);
    message("", text, 125);
    message("5", text, 125);
    message("-k 1", text, 125);
    snprintf(text, sizeof text, "timeout: invalid time interval 'abc'\n%s", usage);
    message("abc sleep 1", text, 125);
    message("-k abc 5 sleep 1", text, 125);
    snprintf(text, sizeof text, "timeout: invalid time interval '1x'\n%s", usage);
    message("1x sleep 1", text, 125);
    snprintf(text, sizeof text, "timeout: invalid time interval '1m1'\n%s", usage);
    message("1m1 sleep 1", text, 125);
    snprintf(text, sizeof text, "timeout: invalid time interval 'nan'\n%s", usage);
    message("nan sleep 1", text, 125);
    snprintf(text, sizeof text, "timeout: invalid time interval '1e'\n%s", usage);
    message("1e sleep 1", text, 125);
    snprintf(text, sizeof text, "timeout: 'FOO': invalid signal\n%s", usage);
    message("-s FOO 1 sleep 1", text, 125);
    snprintf(text, sizeof text, "timeout: '99': invalid signal\n%s", usage);
    message("-s 99 1 sleep 1", text, 125);
    snprintf(text, sizeof text, "timeout: option requires an argument -- 's'\n%s", usage);
    message("-s", text, 125);
    snprintf(text, sizeof text, "timeout: unrecognized option '--bogus'\n%s", usage);
    message("--bogus 1 sleep 1", text, 125);

    printf("a command that cannot be run\n");
    message("1 nosuchcommand_zz", "timeout: failed to run command 'nosuchcommand_zz': No such file or directory\n", 127);
    message("1 /nonexistent/x", "timeout: failed to run command '/nonexistent/x': No such file or directory\n", 127);
    expect_run("timeout", "--version", "", "timeout (Winshell) 0.1\n", 0);
    return test_summary("timeout_test");
}
