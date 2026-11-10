/*
 * Tests of chgrp, run as the program it is: every file is in the primary group of the current user, so naming that group
 * changes nothing, naming another one is refused, and the names and files that do not exist are errors. GNU chgrp is
 * compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void message(const char* arguments, const char* part, int status) {
    RunResult r;

    run_tool(workDir, "chgrp", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

int main(void) {
    RunResult r;
    char group[256], arguments[512], expected[512];

    tools_init("chgrp_test");
    reset_dir(workDir);
    write_text(workDir, "f.txt", "x");
    make_directory("d");

    run_tool(workDir, "id", "-gn", "", &r);
    snprintf(group, sizeof group, "%s", r.out != NULL ? r.out : "");
    group[strcspn(group, "\n")] = '\0';
    run_result_free(&r);

    printf("the primary group\n");
    snprintf(arguments, sizeof arguments, "-v %s f.txt", group);
    snprintf(expected, sizeof expected, "group of 'f.txt' retained as %s\n", group);
    expect_run("chgrp", arguments, "", expected, 0);
    snprintf(arguments, sizeof arguments, "%s f.txt d", group);
    expect_run("chgrp", arguments, "", "", 0);
    snprintf(arguments, sizeof arguments, "-c %s f.txt", group);
    expect_run("chgrp", arguments, "", "", 0);
    snprintf(arguments, sizeof arguments, "-v --reference=f.txt d");
    run_tool(workDir, "chgrp", arguments, "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(r.out != NULL && strstr(r.out, "group of 'd' retained as ") == r.out);
    run_result_free(&r);

    printf("another group\n");
    message("Users f.txt", "changing group of 'f.txt': Operation not permitted", 1);
    message("12345 f.txt", "changing group of 'f.txt': Operation not permitted", 1);

    printf("errors\n");
    message("nosuchgroup_zz f.txt", "invalid group: 'nosuchgroup_zz'", 1);
    snprintf(arguments, sizeof arguments, "%s nosuchfile", group);
    message(arguments, "cannot access 'nosuchfile': No such file or directory", 1);
    message("", "missing operand", 1);
    snprintf(arguments, sizeof arguments, "%s", group);
    message(arguments, "missing operand after", 1);
    message("--bogus x f.txt", "unrecognized option", 1);
    expect_run("chgrp", "--version", "", "chgrp (Winshell) 0.1\n", 0);
    return test_summary("chgrp_test");
}
