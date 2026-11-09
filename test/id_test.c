/*
 * Tests of id, run as the program it is: the formats (default, -u -g -G, -n, -z), the errors of the option
 * combinations, the users named, and that the numbers agree with each other. The IDs are the relative IDs of the
 * account, so what is compared is how the outputs fit together, not fixed numbers.
 * GNU id is compared with by gnu_compare.sh (errors and exit codes).
 */

#include "testtools.h"

static void expect(const char* arguments, int status) {
    expect_run("id", arguments, "", "", status);
}

static void message(const char* arguments, const char* part) {
    RunResult r;

    run_tool(workDir, "id", arguments, "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK_STR(r.out, "");
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

/** The output of id with these arguments (a line, without its end); empty when it failed. */
static void line(const char* arguments, char* out, size_t size) {
    RunResult r;
    size_t n;

    run_tool(workDir, "id", arguments, "", &r);
    snprintf(out, size, "%s", r.out == NULL ? "" : r.out);
    n = strlen(out);
    if (n > 0 && out[n - 1] == '\n')
        out[n - 1] = '\0';
    run_result_free(&r);
}

int main(void) {
    char full[4096], user[256], name[256], group[256], groups[2048], expected[4096];
    RunResult r;

    tools_init("id_test");

    printf("default format\n");
    line("", full, sizeof full);
    line("-u", user, sizeof user);
    line("-un", name, sizeof name);
    line("-g", group, sizeof group);
    line("-G", groups, sizeof groups);
    CHECK(strncmp(full, "uid=", 4) == 0);
    snprintf(expected, sizeof expected, "uid=%s(%s) gid=%s", user, name, group);
    CHECK(strncmp(full, expected, strlen(expected)) == 0);          // uid=N(name) gid=N(...) groups=...
    CHECK(strstr(full, " groups=") != NULL);
    CHECK(strlen(user) > 0 && strlen(name) > 0);

    printf("one kind of ID\n");
    CHECK(strncmp(groups, group, strlen(group)) == 0);              // the primary group comes first
    line("-ur", expected, sizeof expected);
    CHECK_STR(expected, user);                                      // the real ID is the effective ID
    line("-gr", expected, sizeof expected);
    CHECK_STR(expected, group);
    line("-a -u", expected, sizeof expected);
    CHECK_STR(expected, user);                                      // -a is ignored
    line("--user", expected, sizeof expected);
    CHECK_STR(expected, user);
    line("--name --user", expected, sizeof expected);
    CHECK_STR(expected, name);
    run_tool(workDir, "id", "-Gn", "", &r);                         // the names of the groups, one per ID
    CHECK_INT(r.exitCode, 0);
    CHECK(r.out != NULL && strchr(r.out, '\n') == r.out + strlen(r.out) - 1);
    run_result_free(&r);

    printf("-z\n");
    snprintf(expected, sizeof expected, "%s", user);
    run_tool(workDir, "id", "-zu", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(r.outLength == strlen(user) + 1 && memcmp(r.out, user, strlen(user)) == 0 && r.out[strlen(user)] == '\0');
    run_result_free(&r);
    run_tool(workDir, "id", "-zG", "", &r);
    CHECK(r.outLength > 0 && r.out[r.outLength - 1] == '\0' && strchr(r.out, '\n') == NULL);
    run_result_free(&r);

    printf("the users named\n");
    snprintf(expected, sizeof expected, "-u %s", name);
    line(expected, group, sizeof group);
    CHECK_STR(group, user);
    snprintf(expected, sizeof expected, "-un %s %s", name, name);
    run_tool(workDir, "id", expected, "", &r);
    CHECK_INT(r.exitCode, 0);
    snprintf(expected, sizeof expected, "%s\n%s\n", name, name);
    CHECK_STR(r.out, expected);
    run_result_free(&r);
    run_tool(workDir, "id", "-u nosuchuser_x", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK_STR(r.out, "");
    CHECK(strstr(r.err, "'nosuchuser_x': no such user") != NULL);
    run_result_free(&r);
    snprintf(expected, sizeof expected, "-u nosuchuser_x %s", name);    // the others are still shown
    run_tool(workDir, "id", expected, "", &r);
    CHECK_INT(r.exitCode, 1);
    snprintf(expected, sizeof expected, "%s\n", user);
    CHECK_STR(r.out, expected);
    run_result_free(&r);

    printf("errors\n");
    message("-u -g", "cannot print \"only\" of more than one choice");
    message("-gG", "cannot print \"only\" of more than one choice");
    message("-n", "cannot print only names or real IDs in default format");
    message("-r", "cannot print only names or real IDs in default format");
    message("-z", "option --zero not permitted in default format");
    message("-zn", "cannot print only names or real IDs in default format");
    message("-Z", "--context (-Z) works only on an SELinux-enabled kernel");
    expect("--bogus", 1);
    expect("-x", 1);
    expect_run("id", "--version", "", "id (Winshell) 0.1\n", 0);

    return test_summary("id_test");
}
