/*
 * Tests of sha224sum, run as the program it is: the digests of the usual strings, the line formats (-b, -t, --tag, -z),
 * checking with -c and the errors. GNU sha224sum is compared with by gnu_compare.sh (all the *sum tools together).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("sha224sum", arguments, input, expected, status);
}

int main(void) {
    tools_init("sha224sum_test");
    reset_dir(workDir);

    printf("digests\n");
    expect("", "", "d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f *-\n", 0);
    expect("", "abc", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 *-\n", 0);
    expect("", "The quick brown fox jumps over the lazy dog", "730e109bd7a8a32b1cb9d9a09aa2325d2430587ddbc0c38bad911525 *-\n", 0);
    expect("-", "abc", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 *-\n", 0);

    printf("line formats\n");
    write_text(workDir, "abc.txt", "abc");
    expect("abc.txt", "", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 *abc.txt\n", 0);
    expect("-b abc.txt", "", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 *abc.txt\n", 0);
    expect("-t abc.txt", "", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7  abc.txt\n", 0);
    expect("--tag abc.txt", "", "SHA224 (abc.txt) = 23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7\n", 0);
    expect("-z abc.txt", "", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 *abc.txt\0", 0);
    expect("abc.txt abc.txt", "", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 *abc.txt\n23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 *abc.txt\n", 0);

    printf("checking\n");
    write_text(workDir, "good.txt", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 *abc.txt\n23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7  abc.txt\n");
    expect("-c good.txt", "", "abc.txt: OK\nabc.txt: OK\n", 0);
    expect("-c --quiet good.txt", "", "", 0);
    expect("-c --status good.txt", "", "", 0);
    write_text(workDir, "bsd.txt", "SHA224 (abc.txt) = 23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7\n");
    expect("-c bsd.txt", "", "abc.txt: OK\n", 0);
    write_text(workDir, "bad.txt", "d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f *abc.txt\n");
    expect("-c bad.txt", "", "abc.txt: FAILED\n", 1);
    write_text(workDir, "gone.txt", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 *nosuchfile\n");
    expect("-c gone.txt", "", "nosuchfile: FAILED open or read\n", 1);
    expect("-c --ignore-missing gone.txt", "", "", 1);                  // nothing was verified
    write_text(workDir, "upper.txt", "23097D223405D8228642A477BDA255B32AADBCE4BDA0B3F7E36C9DA7 *abc.txt\n");
    expect("-c upper.txt", "", "abc.txt: OK\n", 0);                    // the digits may be upper case
    write_text(workDir, "junk.txt", "junk\n");
    expect("-c junk.txt", "", "", 1);                                  // no line is a checksum
    write_text(workDir, "partly.txt", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 *abc.txt\njunk\n");
    expect("-c partly.txt", "", "abc.txt: OK\n", 0);
    expect("-c --strict partly.txt", "", "abc.txt: OK\n", 1);
    expect("-c", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 *abc.txt\n", "abc.txt: OK\n", 0);              // the list from standard input

    printf("errors\n");
    expect("nosuchfile", "", "", 1);
    expect("abc.txt nosuchfile", "", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 *abc.txt\n", 1);          // the others are still done
    expect("--tag -c good.txt", "", "", 1);
    expect("--status abc.txt", "", "", 1);                             // only for -c
    expect("-x", "", "", 1);
    expect("--version", "", "sha224sum (Winshell) 0.1\n", 0);

    return test_summary("sha224sum_test");
}
