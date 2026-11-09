/*
 * Tests of od, run as the program it is: the default octal words, the types and sizes, the address radix, -j -N, the
 * lines of equal bytes ("*"), several types one under the other, the z trailer, -S, --endian and the errors.
 * GNU od is compared with by gnu_compare.sh (the same on many kinds of input).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("od", arguments, input, expected, status);
}

int main(void) {
    tools_init("od_test");
    reset_dir(workDir);

    printf("default\n");
    expect("", "", "0000000\n", 0);
    expect("", "ab", "0000000 061141\n0000002\n", 0);
    expect("", "abc", "0000000 061141 000143\n0000003\n", 0);              // the last word is filled with zeros
    expect("-An", "ab", " 061141\n", 0);

    printf("types\n");
    expect("-c", "hello world 123\n", "0000000   h   e   l   l   o       w   o   r   l   d       1   2   3  \\n\n0000020\n", 0);
    expect("-An -t x1z", "hello world 123\n", " 68 65 6c 6c 6f 20 77 6f 72 6c 64 20 31 32 33 0a  >hello world 123.<\n", 0);
    expect("-t x1", "ab", "0000000 61 62\n0000002\n", 0);
    expect("-t u1", "ab", "0000000  97  98\n0000002\n", 0);
    expect("-t d1", "\x01\xff", "0000000    1   -1\n0000002\n", 0);
    expect("-t d4", "\x01\x02\x03\x04", "0000000    67305985\n0000004\n", 0);
    expect("--endian=big -t u4", "\x01\x02\x03\x04", "0000000   16909060\n0000004\n", 0);
    expect("-t a", "a\n\x7f", "0000000   a  nl del\n0000003\n", 0);
    expect("-t o1", "abc", "0000000 141 142 143\n0000003\n", 0);
    expect("-b", "abc", "0000000 141 142 143\n0000003\n", 0);
    expect("-t f4", "\xdb\x0f\x49\x40", "0000000       3.1415927\n0000004\n", 0);
    expect("-t f8", "\x18\x2d\x44\x54\xfb\x21\x09\x40", "0000000        3.141592653589793\n0000010\n", 0);
    expect("-t x1 -t c", "ab", "0000000  61  62\n          a   b\n0000002\n", 0);       // one under the other, with the address once

    printf("addresses, skipping, limits\n");
    expect("-A x -t x1", "ab", "000000 61 62\n000002\n", 0);
    expect("-A d -t x1", "ab", "0000000 61 62\n0000002\n", 0);
    expect("-A n -t x1", "ab", " 61 62\n", 0);
    expect("-j1 -c", "abc", "0000001   b   c\n0000003\n", 0);
    expect("-j1 -N1 -c", "abc", "0000001   b\n0000002\n", 0);
    expect("-N0 -c", "abc", "0000000\n", 0);
    expect("-j4 -c", "abc", "", 1);                                          // past the end
    expect("-w4 -t x1", "abcdefg", "0000000 61 62 63 64\n0000004 65 66 67\n0000007\n", 0);

    printf("equal lines\n");
    expect("-c", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
           "0000000   a   a   a   a   a   a   a   a   a   a   a   a   a   a   a   a\n*\n0000100\n", 0);
    expect("-v -w2 -t c", "aaaa", "0000000   a   a\n0000002   a   a\n0000004\n", 0);

    printf("strings\n");
    expect("-S 3", "abcdef", "", 0);                                          // no NUL: no string (GNU compare has the rest)

    printf("files\n");
    write_text(workDir, "f1.txt", "abc");
    write_text(workDir, "f2.txt", "defg");
    expect("-t x1 f1.txt f2.txt", "", "0000000 61 62 63 64 65 66 67\n0000007\n", 0);
    expect("-t x1 -j 4 f1.txt f2.txt", "", "0000004 65 66 67\n0000007\n", 0);
    expect("-t x1 f1.txt nosuchfile f2.txt", "", "0000000 61 62 63 64 65 66 67\n0000007\n", 1);
    expect("-t x1 nosuchfile", "", "", 1);

    printf("errors\n");
    expect("-t d3", "", "", 1);
    expect("-t q", "", "", 1);
    expect("-t c2", "", "", 1);
    expect("-A q", "", "", 1);
    expect("-j x", "", "", 1);
    expect("-N -1", "", "", 1);
    expect("--endian=x", "", "", 1);
    expect("-S 3 -t x1", "", "", 1);
    expect("--bogus", "", "", 1);
    expect("--version", "", "od (Winshell) 0.1\n", 0);

    return test_summary("od_test");
}
