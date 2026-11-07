/*
 * Tests of base64, run as the program it is: the vectors of RFC 4648, the lines of 76, -w, decoding with its
 * errors and -i. GNU base64 is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("base64", arguments, input, expected, status);
}

int main(void) {
    tools_init("base64_test");

    printf("encoding\n");
    expect("", "", "", 0);
    expect("", "f", "Zg==\n", 0);
    expect("", "fo", "Zm8=\n", 0);
    expect("", "foo", "Zm9v\n", 0);
    expect("", "foob", "Zm9vYg==\n", 0);
    expect("", "fooba", "Zm9vYmE=\n", 0);
    expect("", "foobar", "Zm9vYmFy\n", 0);
    expect("", "\xff\xfe\xfd", "//79\n", 0);
    expect("-w 4", "foobar", "Zm9v\nYmFy\n", 0);
    expect("-w 0", "foobar", "Zm9vYmFy", 0);
    expect("--wrap=5", "foobar", "Zm9vY\nmFy\n", 0);
    // 57 bytes are 76 characters: the first line is full
    expect("", "012345678901234567890123456789012345678901234567890123456",
           "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2\n", 0);

    printf("decoding\n");
    expect("-d", "Zm9vYmFy\n", "foobar", 0);
    expect("-d", "Zm9vYg==", "foob", 0);
    expect("-d", "Zm9v\nYmFy\n", "foobar", 0);
    expect("--decode", "Zg==Zg==", "ff", 0);
    expect("-d", "", "", 0);
    // invalid input: what could be decoded is written, then the status is 1
    expect("-d", "Zm9vYg", "foob", 1);
    expect("-d", "Zm9v!", "foo", 1);
    expect("-d", "Zm9v Ymfy", "foo", 1);
    expect("-d", "=", "", 1);
    expect("-d", "Zg=a", "f", 1);
    expect("-di", "Zm9v Y!mFy", "foobar", 0);

    printf("files and errors\n");
    write_text(workDir, "in.txt", "hello");
    expect("in.txt", "", "aGVsbG8=\n", 0);
    expect("nosuchfile", "", "", 1);
    expect("a b", "", "", 1);
    expect("-w x", "", "", 1);
    expect("-x", "", "", 1);
    expect("--version", "", "base64 (Winshell) 0.1\n", 0);

    return test_summary("base64_test");
}
