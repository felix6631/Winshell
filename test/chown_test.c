/*
 * Tests of chown, run as the program it is: every file is owned by the current user, so naming that user (and its group)
 * changes nothing and is reported as retained, naming another one is refused, and the names, files and options that do
 * not exist are errors. GNU chown is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void message(const char* arguments, const char* part, int status) {
    RunResult r;

    run_tool(workDir, "chown", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

int main(void) {
    RunResult r;
    char me[256], arguments[512], expected[512];

    tools_init("chown_test");
    reset_dir(workDir);
    write_text(workDir, "f.txt", "x");
    make_directory("d");
    write_text(workDir, "d\\inner.txt", "y");

    run_tool(workDir, "id", "-un", "", &r);
    snprintf(me, sizeof me, "%s", r.out != NULL ? r.out : "");
    me[strcspn(me, "\n")] = '\0';
    run_result_free(&r);

    printf("the current user\n");
    snprintf(arguments, sizeof arguments, "-v %s f.txt", me);
    snprintf(expected, sizeof expected, "ownership of 'f.txt' retained as %s\n", me);
    expect_run("chown", arguments, "", expected, 0);
    snprintf(arguments, sizeof arguments, "%s f.txt", me);
    expect_run("chown", arguments, "", "", 0);
    snprintf(arguments, sizeof arguments, "-c %s f.txt", me);
    expect_run("chown", arguments, "", "", 0);                                  // nothing changes: nothing is said
    snprintf(arguments, sizeof arguments, "-v -R %s d", me);
    snprintf(expected, sizeof expected, "ownership of 'd' retained as %s\nownership of 'd/inner.txt' retained as %s\n", me, me);
    expect_run("chown", arguments, "", expected, 0);
    snprintf(arguments, sizeof arguments, "-v : f.txt");
    expect_run("chown", arguments, "", "ownership of 'f.txt' retained\n", 0);
    snprintf(arguments, sizeof arguments, "-v --reference=f.txt d");
    run_tool(workDir, "chown", arguments, "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(r.out != NULL && strstr(r.out, "ownership of 'd' retained as ") == r.out);
    run_result_free(&r);

    printf("another owner\n");
    message("54321 f.txt", "changing ownership of 'f.txt': Operation not permitted", 1);
    message("12345 f.txt", "changing ownership of 'f.txt': Operation not permitted", 1);

    printf("errors\n");
    message("nosuchuser_zz f.txt", "invalid user: 'nosuchuser_zz'", 1);
    message(":nosuchgroup_zz f.txt", "invalid group: ':nosuchgroup_zz'", 1);
    snprintf(arguments, sizeof arguments, "%s nosuchfile", me);
    message(arguments, "cannot access 'nosuchfile': No such file or directory", 1);
    message("", "missing operand", 1);
    snprintf(arguments, sizeof arguments, "%s", me);
    message(arguments, "missing operand after", 1);
    message("--reference=nosuchfile f.txt", "failed to get attributes of 'nosuchfile'", 1);
    message("--bogus x f.txt", "unrecognized option", 1);
    expect_run("chown", "--version", "", "chown (Winshell) 0.1\n", 0);
    return test_summary("chown_test");
}
