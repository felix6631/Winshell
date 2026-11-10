/*
 * Tests of stdbuf, run as the program it is: the modes are checked as GNU stdbuf checks them (L, 0, sizes with a suffix;
 * line buffering is meaningless for the input), the status is 125 for an error of stdbuf, 127 and 126 for a command that cannot
 * be run, and the command is run as it is - Windows has no way to change the buffering of another program. GNU stdbuf is
 * compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("stdbuf", arguments, input, expected, status);
}

static void message(const char* arguments, const char* expectedError, int status) {
    RunResult r;

    run_tool(workDir, "stdbuf", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    lf_only(r.err);
    if (strcmp(r.err, expectedError) != 0)
        printf("    for: stdbuf %s\n    got: \"%s\"\n", arguments, r.err);
    CHECK_STR(r.err, expectedError);
    run_result_free(&r);
}

int main(void) {
    const char* usage = "Try 'stdbuf --help' for more information.\n";
    char text[256];

    tools_init("stdbuf_test");
    reset_dir(workDir);

    printf("running the command\n");
    expect("-oL echo hi", "", "hi\n", 0);
    expect("-o L echo hi", "", "hi\n", 0);
    expect("--output=L echo hi", "", "hi\n", 0);
    expect("-o0 -e0 -i0 echo hi", "", "hi\n", 0);
    expect("-i 8 cat", "in\n", "in\n", 0);                                   // standard input and output reach the command
    expect("-o 10K cat", "x\ny\n", "x\ny\n", 0);
    expect("-o 1KiB -e 5MB echo a b", "", "a b\n", 0);
    expect("-o 5k echo hi", "", "hi\n", 0);
    expect("-o 1TiB echo hi", "", "hi\n", 0);
    expect("-o \" 1\" echo hi", "", "hi\n", 0);                                // blanks in front of a number are fine
    expect("-oL -- echo -n x", "", "x", 0);
    expect("-oL echo -n x", "", "x", 0);                                     // after the command the options are its own
    expect("-oL false", "", "", 1);                                          // the status of the command

    printf("the modes\n");
    snprintf(text, sizeof text, "stdbuf: line buffering stdin is meaningless\n%s", usage);
    message("-i L echo hi", text, 125);
    message("-iL echo hi", text, 125);
    message("-o x echo hi", "stdbuf: invalid mode 'x'\n", 125);
    message("-o 1x echo hi", "stdbuf: invalid mode '1x'\n", 125);
    message("-o 5b echo hi", "stdbuf: invalid mode '5b'\n", 125);
    message("-o 1.5 echo hi", "stdbuf: invalid mode '1.5'\n", 125);
    message("-o -1 echo hi", "stdbuf: invalid mode '-1'\n", 125);
    message("-o 0x10 echo hi", "stdbuf: invalid mode '0x10'\n", 125);
    message("-o \"\" echo hi", "stdbuf: invalid mode ''\n", 125);
    message("-o 99999999999999999999999 echo hi", "stdbuf: invalid mode '99999999999999999999999': Value too large for defined data type\n", 125);
    message("-o 1YiB echo hi", "stdbuf: invalid mode '1YiB': Value too large for defined data type\n", 125);

    printf("operands and options\n");
    snprintf(text, sizeof text, "stdbuf: missing operand\n%s", usage);
    message("", text, 125);
    message("-o L", text, 125);
    snprintf(text, sizeof text, "stdbuf: you must specify a buffering mode option\n%s", usage);
    message("echo hi", text, 125);
    snprintf(text, sizeof text, "stdbuf: option requires an argument -- 'i'\n%s", usage);
    message("-i", text, 125);
    snprintf(text, sizeof text, "stdbuf: unrecognized option '--bogus'\n%s", usage);
    message("--bogus", text, 125);
    message("-o L nosuchcommand_zz", "stdbuf: failed to run command 'nosuchcommand_zz': No such file or directory\n", 127);
    expect_run("stdbuf", "--version", "", "stdbuf (Winshell) 0.1\n", 0);
    return test_summary("stdbuf_test");
}
