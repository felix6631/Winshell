/*
 * Tests of groups, run as the program it is: the names of the groups of the current user and of the users named
 * ("user : group group"), and the errors. The groups are those of the account, so they are compared with id.
 * GNU groups is compared with by gnu_compare.sh (errors and exit codes).
 */

#include "testtools.h"

int main(void) {
    RunResult r, names, user;
    char arguments[512], expected[8192];

    tools_init("groups_test");

    printf("the current user\n");
    run_tool(workDir, "groups", "", "", &r);
    run_tool(workDir, "id", "-Gn", "", &names);
    CHECK_INT(r.exitCode, 0);
    CHECK(r.out != NULL && strlen(r.out) > 1);
    CHECK_STR(r.out, names.out);                                  // the same names as id -Gn
    run_result_free(&r);
    run_result_free(&names);

    printf("the users named\n");
    run_tool(workDir, "id", "-un", "", &user);
    CHECK(user.out != NULL && strlen(user.out) > 1);
    user.out[strcspn(user.out, "\n")] = '\0';
    snprintf(arguments, sizeof arguments, "%s", user.out);
    run_tool(workDir, "groups", arguments, "", &r);
    CHECK_INT(r.exitCode, 0);
    run_tool(workDir, "id", "-Gn", "", &names);
    snprintf(expected, sizeof expected, "%s : %s", user.out, names.out);
    CHECK_STR(r.out, expected);
    run_result_free(&r);
    run_result_free(&names);
    snprintf(arguments, sizeof arguments, "nosuchuser_x %s", user.out);
    run_tool(workDir, "groups", arguments, "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strncmp(r.out, user.out, strlen(user.out)) == 0);       // the user that exists is still shown
    CHECK(strstr(r.err, "'nosuchuser_x': no such user") != NULL);
    run_result_free(&r);
    run_result_free(&user);

    printf("errors\n");
    expect_run("groups", "--bogus", "", "", 1);
    expect_run("groups", "--version", "", "groups (Winshell) 0.1\n", 0);
    return test_summary("groups_test");
}
