/*
 * Tests of stty, run as the program it is. The tests have no console, so what they can check is what stty does before it
 * looks at the terminal: the names of the settings, the missing arguments, the exclusive output styles, and the message of
 * a device that is not a terminal. Showing and setting the modes needs a console and is tried by hand. GNU stty is compared
 * with by gnu_compare.sh.
 */

#include "testtools.h"

static void message(const char* arguments, const char* expectedError) {
    RunResult r;

    run_tool(workDir, "stty", arguments, "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK_STR(r.out, "");
    lf_only(r.err);
    if (strcmp(r.err, expectedError) != 0)
        printf("    for: stty %s\n    got: \"%s\"\n", arguments, r.err);
    CHECK_STR(r.err, expectedError);
    run_result_free(&r);
}

int main(void) {
    const char* notTty = "stty: 'standard input': Inappropriate ioctl for device\n";
    const char* usage = "Try 'stty --help' for more information.\n";
    char text[256];

    tools_init("stty_test");
    reset_dir(workDir);
    write_text(workDir, "plain.txt", "x");

    printf("not a terminal\n");
    message("", notTty);
    message("-a", notTty);
    message("--all", notTty);
    message("-g", notTty);
    message("--save", notTty);
    message("size", notTty);
    message("echo", notTty);
    message("-echo", notTty);
    message("sane", notTty);
    message("raw", notTty);
    message("rows 10", notTty);
    message("cols x", notTty);
    message("9600", notTty);
    message("intr ^C", notTty);
    message("-F plain.txt", "stty: plain.txt: Inappropriate ioctl for device\n");
    message("--file=plain.txt -a", "stty: plain.txt: Inappropriate ioctl for device\n");
    message("-F plain.txt size", "stty: plain.txt: Inappropriate ioctl for device\n");
    message("-F nosuchfile_zz", "stty: nosuchfile_zz: No such file or directory\n");

    printf("the arguments\n");
    snprintf(text, sizeof text, "stty: invalid argument 'foo'\n%s", usage);
    message("foo", text);
    message("echo foo", text);
    message("-F plain.txt foo", text);
    snprintf(text, sizeof text, "stty: invalid argument '--bogus'\n%s", usage);
    message("--bogus", text);
    snprintf(text, sizeof text, "stty: invalid argument '-x'\n%s", usage);
    message("-x", text);
    snprintf(text, sizeof text, "stty: invalid argument '-F'\n%s", usage);
    message("-F", text);
    snprintf(text, sizeof text, "stty: invalid argument 'extra'\n%s", usage);
    message("size extra", text);
    snprintf(text, sizeof text, "stty: missing argument to 'rows'\n%s", usage);
    message("rows", text);
    snprintf(text, sizeof text, "stty: missing argument to 'intr'\n%s", usage);
    message("echo intr", text);
    message("-a -g", "stty: the options for verbose and stty-readable output styles are\nmutually exclusive\n");
    message("-g size", "stty: when specifying an output style, modes may not be set\n");
    message("-a echo", "stty: when specifying an output style, modes may not be set\n");
    expect_run("stty", "--version", "", "stty (Winshell) 0.1\n", 0);
    return test_summary("stty_test");
}
