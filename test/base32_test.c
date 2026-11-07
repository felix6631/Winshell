/*
 * Tests of base32, run as the program it is: the vectors of RFC 4648, -w, decoding and its errors.
 * GNU base32 is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("base32", arguments, input, expected, status);
}

int main(void) {
    tools_init("base32_test");

    printf("encoding\n");
    expect("", "", "", 0);
    expect("", "f", "MY======\n", 0);
    expect("", "fo", "MZXQ====\n", 0);
    expect("", "foo", "MZXW6===\n", 0);
    expect("", "foob", "MZXW6YQ=\n", 0);
    expect("", "fooba", "MZXW6YTB\n", 0);
    expect("", "foobar", "MZXW6YTBOI======\n", 0);
    expect("-w 8", "foobar", "MZXW6YTB\nOI======\n", 0);
    expect("-w 0", "foobar", "MZXW6YTBOI======", 0);

    printf("decoding\n");
    expect("-d", "MZXW6YTBOI======\n", "foobar", 0);
    expect("-d", "MZXW6===", "foo", 0);
    expect("-d", "MZXW6YTB\nOI======\n", "foobar", 0);
    expect("-d", "", "", 0);
    expect("-d", "MZXW6", "", 1);                 // the end in the middle of a group
    expect("-d", "MZXW6!!!", "", 1);
    expect("-d", "mzxw6===", "", 1);              // lower case is not in the alphabet
    expect("-di", "MZXW 6YTB", "fooba", 0);

    printf("errors\n");
    expect("nosuchfile", "", "", 1);
    expect("a b", "", "", 1);
    expect("-w x", "", "", 1);
    expect("--version", "", "base32 (Winshell) 0.1\n", 0);

    return test_summary("base32_test");
}
