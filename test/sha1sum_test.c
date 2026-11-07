/*
 * Tests of sha1sum, run as the program it is: the digests of the usual strings, the line formats (-b, -t, --tag, -z),
 * checking with -c and the errors. GNU sha1sum is compared with by gnu_compare.sh (all the *sum tools together).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("sha1sum", arguments, input, expected, status);
}

int main(void) {
    tools_init("sha1sum_test");
    reset_dir(workDir);

    printf("digests\n");
    expect("", "", "da39a3ee5e6b4b0d3255bfef95601890afd80709 *-\n", 0);
    expect("", "abc", "a9993e364706816aba3e25717850c26c9cd0d89d *-\n", 0);
    expect("", "The quick brown fox jumps over the lazy dog", "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12 *-\n", 0);
    expect("-", "abc", "a9993e364706816aba3e25717850c26c9cd0d89d *-\n", 0);

    printf("line formats\n");
    write_text(workDir, "abc.txt", "abc");
    expect("abc.txt", "", "a9993e364706816aba3e25717850c26c9cd0d89d *abc.txt\n", 0);
    expect("-b abc.txt", "", "a9993e364706816aba3e25717850c26c9cd0d89d *abc.txt\n", 0);
    expect("-t abc.txt", "", "a9993e364706816aba3e25717850c26c9cd0d89d  abc.txt\n", 0);
    expect("--tag abc.txt", "", "SHA1 (abc.txt) = a9993e364706816aba3e25717850c26c9cd0d89d\n", 0);
    expect("-z abc.txt", "", "a9993e364706816aba3e25717850c26c9cd0d89d *abc.txt\0", 0);
    expect("abc.txt abc.txt", "", "a9993e364706816aba3e25717850c26c9cd0d89d *abc.txt\na9993e364706816aba3e25717850c26c9cd0d89d *abc.txt\n", 0);

    printf("checking\n");
    write_text(workDir, "good.txt", "a9993e364706816aba3e25717850c26c9cd0d89d *abc.txt\na9993e364706816aba3e25717850c26c9cd0d89d  abc.txt\n");
    expect("-c good.txt", "", "abc.txt: OK\nabc.txt: OK\n", 0);
    expect("-c --quiet good.txt", "", "", 0);
    expect("-c --status good.txt", "", "", 0);
    write_text(workDir, "bsd.txt", "SHA1 (abc.txt) = a9993e364706816aba3e25717850c26c9cd0d89d\n");
    expect("-c bsd.txt", "", "abc.txt: OK\n", 0);
    write_text(workDir, "bad.txt", "da39a3ee5e6b4b0d3255bfef95601890afd80709 *abc.txt\n");
    expect("-c bad.txt", "", "abc.txt: FAILED\n", 1);
    write_text(workDir, "gone.txt", "a9993e364706816aba3e25717850c26c9cd0d89d *nosuchfile\n");
    expect("-c gone.txt", "", "nosuchfile: FAILED open or read\n", 1);
    expect("-c --ignore-missing gone.txt", "", "", 1);                  // nothing was verified
    write_text(workDir, "upper.txt", "A9993E364706816ABA3E25717850C26C9CD0D89D *abc.txt\n");
    expect("-c upper.txt", "", "abc.txt: OK\n", 0);                    // the digits may be upper case
    write_text(workDir, "junk.txt", "junk\n");
    expect("-c junk.txt", "", "", 1);                                  // no line is a checksum
    write_text(workDir, "partly.txt", "a9993e364706816aba3e25717850c26c9cd0d89d *abc.txt\njunk\n");
    expect("-c partly.txt", "", "abc.txt: OK\n", 0);
    expect("-c --strict partly.txt", "", "abc.txt: OK\n", 1);
    expect("-c", "a9993e364706816aba3e25717850c26c9cd0d89d *abc.txt\n", "abc.txt: OK\n", 0);              // the list from standard input

    printf("errors\n");
    expect("nosuchfile", "", "", 1);
    expect("abc.txt nosuchfile", "", "a9993e364706816aba3e25717850c26c9cd0d89d *abc.txt\n", 1);          // the others are still done
    expect("--tag -c good.txt", "", "", 1);
    expect("--status abc.txt", "", "", 1);                             // only for -c
    expect("-x", "", "", 1);
    expect("--version", "", "sha1sum (Winshell) 0.1\n", 0);

    return test_summary("sha1sum_test");
}
