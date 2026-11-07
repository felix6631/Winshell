/*
 * Tests of tty, run as the program it is. Standard input is a pipe here, never a terminal.
 * GNU tty is compared with by gnu_compare.sh.
 */

#include "testtools.h"

int main(void) {
    tools_init("tty_test");

    printf("tty\n");
    expect_run("tty", "", "", "not a tty\n", 1);
    expect_run("tty", "-s", "", "", 1);
    expect_run("tty", "--silent", "", "", 1);
    expect_run("tty", "--quiet", "", "", 1);
    expect_run("tty", "extra", "", "", 2);
    expect_run("tty", "-x", "", "", 2);
    expect_run("tty", "--version", "", "tty (Winshell) 0.1\n", 0);
    return test_summary("tty_test");
}
