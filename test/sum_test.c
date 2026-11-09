/*
 * Tests of sum, run as the program it is: the BSD checksum and the System V one, the blocks, with and without a name.
 * GNU sum is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("sum", arguments, input, expected, status);
}

int main(void) {
    tools_init("sum_test");
    reset_dir(workDir);

    printf("BSD\n");
    expect("", "", "00000     0\n", 0);
    expect("", "hello\n", "36979     1\n", 0);
    expect("-r", "hello\n", "36979     1\n", 0);

    printf("System V\n");
    expect("-s", "", "0 0\n", 0);
    expect("-s", "hello\n", "542 1\n", 0);
    expect("--sysv", "hello\n", "542 1\n", 0);
    expect("-sr", "hello\n", "36979     1\n", 0);                  // the last one wins
    expect("-rs", "hello\n", "542 1\n", 0);

    printf("names\n");
    write_text(workDir, "h.txt", "hello\n");
    expect("h.txt", "", "36979     1\n", 0);                        // one operand: no name
    expect("-", "hello\n", "36979     1\n", 0);
    expect("-s h.txt", "", "542 1 h.txt\n", 0);                      // System V: a single operand has its name too
    expect("-s -", "hello\n", "542 1 -\n", 0);
    expect("h.txt h.txt", "", "36979     1 h.txt\n36979     1 h.txt\n", 0);
    expect("-s h.txt -", "hello\n", "542 1 h.txt\n542 1 -\n", 0);
    expect("h.txt nosuchfile", "", "36979     1 h.txt\n", 1);
    expect("--bogus", "", "", 1);
    expect("--version", "", "sum (Winshell) 0.1\n", 0);

    return test_summary("sum_test");
}
