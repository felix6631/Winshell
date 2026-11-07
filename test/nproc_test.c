/*
 * Tests of nproc, run as the program it is. GNU nproc is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static long number_of(const char* arguments) {
    RunResult r;
    long value = -1;

    run_tool(workDir, "nproc", arguments, "", &r);
    if (r.out != NULL)
        value = atol(r.out);
    run_result_free(&r);
    return value;
}

int main(void) {
    SYSTEM_INFO info;
    long all;

    tools_init("nproc_test");
    GetSystemInfo(&info);

    printf("nproc\n");
    all = number_of("--all");
    CHECK(all >= 1 && all >= (long)info.dwNumberOfProcessors);
    CHECK(number_of("") >= 1 && number_of("") <= all);

    CHECK_INT(number_of("--ignore=1"), all > 1 ? all - 1 : 1);
    CHECK_INT(number_of("--all --ignore=1"), all > 1 ? all - 1 : 1);
    CHECK_INT(number_of("--ignore=0"), number_of(""));
    CHECK_INT(number_of("--ignore=100000"), 1);            // never below 1

    expect_run("nproc", "--ignore=x", "", "", 1);
    expect_run("nproc", "extra", "", "", 1);
    expect_run("nproc", "--version", "", "nproc (Winshell) 0.1\n", 0);
    return test_summary("nproc_test");
}
