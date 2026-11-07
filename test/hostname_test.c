/*
 * Tests of hostname, run as the program it is. GNU hostname is compared with by gnu_compare.sh.
 */

#include "testtools.h"

int main(void) {
    RunResult r;
    char expected[300], name[256];
    DWORD size = sizeof name;

    tools_init("hostname_test");

    printf("hostname\n");
    GetComputerNameExA(ComputerNameDnsHostname, name, &size);
    snprintf(expected, sizeof expected, "%s\n", name);
    expect_run("hostname", "", "", expected, 0);

    // -s is the name up to the first dot, -f the fully qualified one (it starts with the host name)
    expect_run("hostname", "-s", "", expected, 0);
    run_tool(workDir, "hostname", "--fqdn", "", &r);
    CHECK(r.out != NULL && strncmp(r.out, name, strlen(name)) == 0);
    run_result_free(&r);
    run_tool(workDir, "hostname", "-d", "", &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    // the name cannot be set
    expect_run("hostname", "newname", "", "", 1);
    expect_run("hostname", "a b", "", "", 1);
    expect_run("hostname", "-x", "", "", 1);
    return test_summary("hostname_test");
}
