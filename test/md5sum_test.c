/*
 * Tests of md5sum, run as the program it is: the digests of the usual strings, the line formats (-b, -t, --tag, -z),
 * checking with -c and the errors. GNU md5sum is compared with by gnu_compare.sh (all the *sum tools together).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("md5sum", arguments, input, expected, status);
}

int main(void) {
    tools_init("md5sum_test");
    reset_dir(workDir);

    printf("digests\n");
    expect("", "", "d41d8cd98f00b204e9800998ecf8427e *-\n", 0);
    expect("", "abc", "900150983cd24fb0d6963f7d28e17f72 *-\n", 0);
    expect("", "The quick brown fox jumps over the lazy dog", "9e107d9d372bb6826bd81d3542a419d6 *-\n", 0);
    expect("-", "abc", "900150983cd24fb0d6963f7d28e17f72 *-\n", 0);

    printf("line formats\n");
    write_text(workDir, "abc.txt", "abc");
    expect("abc.txt", "", "900150983cd24fb0d6963f7d28e17f72 *abc.txt\n", 0);
    expect("-b abc.txt", "", "900150983cd24fb0d6963f7d28e17f72 *abc.txt\n", 0);
    expect("-t abc.txt", "", "900150983cd24fb0d6963f7d28e17f72  abc.txt\n", 0);
    expect("--tag abc.txt", "", "MD5 (abc.txt) = 900150983cd24fb0d6963f7d28e17f72\n", 0);
    expect("-z abc.txt", "", "900150983cd24fb0d6963f7d28e17f72 *abc.txt\0", 0);
    expect("abc.txt abc.txt", "", "900150983cd24fb0d6963f7d28e17f72 *abc.txt\n900150983cd24fb0d6963f7d28e17f72 *abc.txt\n", 0);

    printf("checking\n");
    write_text(workDir, "good.txt", "900150983cd24fb0d6963f7d28e17f72 *abc.txt\n900150983cd24fb0d6963f7d28e17f72  abc.txt\n");
    expect("-c good.txt", "", "abc.txt: OK\nabc.txt: OK\n", 0);
    expect("-c --quiet good.txt", "", "", 0);
    expect("-c --status good.txt", "", "", 0);
    write_text(workDir, "bsd.txt", "MD5 (abc.txt) = 900150983cd24fb0d6963f7d28e17f72\n");
    expect("-c bsd.txt", "", "abc.txt: OK\n", 0);
    write_text(workDir, "bad.txt", "d41d8cd98f00b204e9800998ecf8427e *abc.txt\n");
    expect("-c bad.txt", "", "abc.txt: FAILED\n", 1);
    write_text(workDir, "gone.txt", "900150983cd24fb0d6963f7d28e17f72 *nosuchfile\n");
    expect("-c gone.txt", "", "nosuchfile: FAILED open or read\n", 1);
    expect("-c --ignore-missing gone.txt", "", "", 1);                  // nothing was verified
    write_text(workDir, "upper.txt", "900150983CD24FB0D6963F7D28E17F72 *abc.txt\n");
    expect("-c upper.txt", "", "abc.txt: OK\n", 0);                    // the digits may be upper case
    write_text(workDir, "junk.txt", "junk\n");
    expect("-c junk.txt", "", "", 1);                                  // no line is a checksum
    write_text(workDir, "partly.txt", "900150983cd24fb0d6963f7d28e17f72 *abc.txt\njunk\n");
    expect("-c partly.txt", "", "abc.txt: OK\n", 0);
    expect("-c --strict partly.txt", "", "abc.txt: OK\n", 1);
    expect("-c", "900150983cd24fb0d6963f7d28e17f72 *abc.txt\n", "abc.txt: OK\n", 0);              // the list from standard input

    printf("errors\n");
    expect("nosuchfile", "", "", 1);
    expect("abc.txt nosuchfile", "", "900150983cd24fb0d6963f7d28e17f72 *abc.txt\n", 1);          // the others are still done
    expect("--tag -c good.txt", "", "", 1);
    expect("--status abc.txt", "", "", 1);                             // only for -c
    expect("-x", "", "", 1);
    expect("--version", "", "md5sum (Winshell) 0.1\n", 0);

    return test_summary("md5sum_test");
}
