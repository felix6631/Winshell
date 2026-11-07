/*
 * Tests of yes, run as the program it is. It never ends, so a reader that stops (head) ends it.
 * GNU yes is compared with by gnu_compare.sh (section yes: its options and exit statuses).
 */

#include "testtools.h"

int main(void) {
    RunResult r;

    tools_init("yes_test");

    printf("yes\n");
    SCRIPT_OUT("yes | head -n 3\n", "y\ny\ny\n");
    SCRIPT_OUT("yes hello world | head -n 2\n", "hello world\nhello world\n");
    SCRIPT_OUT("yes '' | head -n 2\n", "\n\n");
    SCRIPT_OUT("yes a b c | head -n 1\n", "a b c\n");
    SCRIPT_OUT("yes -- x | head -n 1\n", "x\n");

    // a long line and many lines: the buffer boundaries
    script("yes | head -n 100000 | wc -l\n", &r);
    CHECK_STR(r.out, "100000\n");
    run_result_free(&r);
    script("yes abcdefghijklmnopqrstuvwxyz0123456789 | head -n 5000 | wc -c\n", &r);
    CHECK_STR(r.out, "185000\n");
    run_result_free(&r);

    expect_run("yes", "--help", "", "Usage: yes [STRING]...\n"
               "  or:  yes OPTION\n"
               "Repeatedly output a line with all specified STRING(s), or 'y'.\n"
               "\n"
               "      --help        display this help and exit\n"
               "      --version     output version information and exit\n", 0);
    expect_run("yes", "--version", "", "yes (Winshell) 0.1\n", 0);

    return test_summary("yes_test");
}
