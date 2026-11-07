/*
 * Tests of sha384sum, run as the program it is: the digests of the usual strings, the line formats (-b, -t, --tag, -z),
 * checking with -c and the errors. GNU sha384sum is compared with by gnu_compare.sh (all the *sum tools together).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("sha384sum", arguments, input, expected, status);
}

int main(void) {
    tools_init("sha384sum_test");
    reset_dir(workDir);

    printf("digests\n");
    expect("", "", "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b *-\n", 0);
    expect("", "abc", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 *-\n", 0);
    expect("", "The quick brown fox jumps over the lazy dog", "ca737f1014a48f4c0b6dd43cb177b0afd9e5169367544c494011e3317dbf9a509cb1e5dc1e85a941bbee3d7f2afbc9b1 *-\n", 0);
    expect("-", "abc", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 *-\n", 0);

    printf("line formats\n");
    write_text(workDir, "abc.txt", "abc");
    expect("abc.txt", "", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 *abc.txt\n", 0);
    expect("-b abc.txt", "", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 *abc.txt\n", 0);
    expect("-t abc.txt", "", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7  abc.txt\n", 0);
    expect("--tag abc.txt", "", "SHA384 (abc.txt) = cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7\n", 0);
    expect("-z abc.txt", "", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 *abc.txt\0", 0);
    expect("abc.txt abc.txt", "", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 *abc.txt\ncb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 *abc.txt\n", 0);

    printf("checking\n");
    write_text(workDir, "good.txt", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 *abc.txt\ncb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7  abc.txt\n");
    expect("-c good.txt", "", "abc.txt: OK\nabc.txt: OK\n", 0);
    expect("-c --quiet good.txt", "", "", 0);
    expect("-c --status good.txt", "", "", 0);
    write_text(workDir, "bsd.txt", "SHA384 (abc.txt) = cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7\n");
    expect("-c bsd.txt", "", "abc.txt: OK\n", 0);
    write_text(workDir, "bad.txt", "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b *abc.txt\n");
    expect("-c bad.txt", "", "abc.txt: FAILED\n", 1);
    write_text(workDir, "gone.txt", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 *nosuchfile\n");
    expect("-c gone.txt", "", "nosuchfile: FAILED open or read\n", 1);
    expect("-c --ignore-missing gone.txt", "", "", 1);                  // nothing was verified
    write_text(workDir, "upper.txt", "CB00753F45A35E8BB5A03D699AC65007272C32AB0EDED1631A8B605A43FF5BED8086072BA1E7CC2358BAECA134C825A7 *abc.txt\n");
    expect("-c upper.txt", "", "abc.txt: OK\n", 0);                    // the digits may be upper case
    write_text(workDir, "junk.txt", "junk\n");
    expect("-c junk.txt", "", "", 1);                                  // no line is a checksum
    write_text(workDir, "partly.txt", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 *abc.txt\njunk\n");
    expect("-c partly.txt", "", "abc.txt: OK\n", 0);
    expect("-c --strict partly.txt", "", "abc.txt: OK\n", 1);
    expect("-c", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 *abc.txt\n", "abc.txt: OK\n", 0);              // the list from standard input

    printf("errors\n");
    expect("nosuchfile", "", "", 1);
    expect("abc.txt nosuchfile", "", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 *abc.txt\n", 1);          // the others are still done
    expect("--tag -c good.txt", "", "", 1);
    expect("--status abc.txt", "", "", 1);                             // only for -c
    expect("-x", "", "", 1);
    expect("--version", "", "sha384sum (Winshell) 0.1\n", 0);

    return test_summary("sha384sum_test");
}
