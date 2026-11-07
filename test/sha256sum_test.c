/*
 * Tests of sha256sum, run as the program it is: the digests of the usual strings, the line formats (-b, -t, --tag, -z),
 * checking with -c and the errors. GNU sha256sum is compared with by gnu_compare.sh (all the *sum tools together).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("sha256sum", arguments, input, expected, status);
}

int main(void) {
    tools_init("sha256sum_test");
    reset_dir(workDir);

    printf("digests\n");
    expect("", "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 *-\n", 0);
    expect("", "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *-\n", 0);
    expect("", "The quick brown fox jumps over the lazy dog", "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592 *-\n", 0);
    expect("-", "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *-\n", 0);

    printf("line formats\n");
    write_text(workDir, "abc.txt", "abc");
    expect("abc.txt", "", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *abc.txt\n", 0);
    expect("-b abc.txt", "", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *abc.txt\n", 0);
    expect("-t abc.txt", "", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  abc.txt\n", 0);
    expect("--tag abc.txt", "", "SHA256 (abc.txt) = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n", 0);
    expect("-z abc.txt", "", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *abc.txt\0", 0);
    expect("abc.txt abc.txt", "", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *abc.txt\nba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *abc.txt\n", 0);

    printf("checking\n");
    write_text(workDir, "good.txt", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *abc.txt\nba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  abc.txt\n");
    expect("-c good.txt", "", "abc.txt: OK\nabc.txt: OK\n", 0);
    expect("-c --quiet good.txt", "", "", 0);
    expect("-c --status good.txt", "", "", 0);
    write_text(workDir, "bsd.txt", "SHA256 (abc.txt) = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n");
    expect("-c bsd.txt", "", "abc.txt: OK\n", 0);
    write_text(workDir, "bad.txt", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 *abc.txt\n");
    expect("-c bad.txt", "", "abc.txt: FAILED\n", 1);
    write_text(workDir, "gone.txt", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *nosuchfile\n");
    expect("-c gone.txt", "", "nosuchfile: FAILED open or read\n", 1);
    expect("-c --ignore-missing gone.txt", "", "", 1);                  // nothing was verified
    write_text(workDir, "upper.txt", "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD *abc.txt\n");
    expect("-c upper.txt", "", "abc.txt: OK\n", 0);                    // the digits may be upper case
    write_text(workDir, "junk.txt", "junk\n");
    expect("-c junk.txt", "", "", 1);                                  // no line is a checksum
    write_text(workDir, "partly.txt", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *abc.txt\njunk\n");
    expect("-c partly.txt", "", "abc.txt: OK\n", 0);
    expect("-c --strict partly.txt", "", "abc.txt: OK\n", 1);
    expect("-c", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *abc.txt\n", "abc.txt: OK\n", 0);              // the list from standard input

    printf("errors\n");
    expect("nosuchfile", "", "", 1);
    expect("abc.txt nosuchfile", "", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *abc.txt\n", 1);          // the others are still done
    expect("--tag -c good.txt", "", "", 1);
    expect("--status abc.txt", "", "", 1);                             // only for -c
    expect("-x", "", "", 1);
    expect("--version", "", "sha256sum (Winshell) 0.1\n", 0);

    return test_summary("sha256sum_test");
}
