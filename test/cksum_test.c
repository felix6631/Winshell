/*
 * Tests of cksum, run as the program it is: the CRC of POSIX and the size, with and without a name.
 * GNU cksum is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("cksum", arguments, input, expected, status);
}

int main(void) {
    tools_init("cksum_test");
    reset_dir(workDir);

    printf("standard input\n");
    expect("", "", "4294967295 0\n", 0);
    expect("", "abc", "1219131554 3\n", 0);
    expect("", "The quick brown fox jumps over the lazy dog", "2074844392 43\n", 0);

    printf("files\n");
    write_text(workDir, "abc.txt", "abc");
    expect("abc.txt", "", "1219131554 3 abc.txt\n", 0);
    expect("- abc.txt", "abc", "1219131554 3 -\n1219131554 3 abc.txt\n", 0);
    expect("abc.txt nosuchfile abc.txt", "", "1219131554 3 abc.txt\n1219131554 3 abc.txt\n", 1);
    expect("-x", "", "", 1);
    expect("--version", "", "cksum (Winshell) 0.1\n", 0);

    return test_summary("cksum_test");
}
