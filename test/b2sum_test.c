/*
 * Tests of b2sum, run as the program it is: BLAKE2b of known inputs, the digest length (-l), the BSD form, checking sums
 * of different lengths and the errors. GNU b2sum is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("b2sum", arguments, input, expected, status);
}

#define ABC512 "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"
#define EMPTY512 "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419d25e1031afee585313896444934eb04b903a685b1448b755d56f701afe9be2ce"

int main(void) {
    tools_init("b2sum_test");
    reset_dir(workDir);

    printf("digests\n");
    expect("", "abc", ABC512 " *-\n", 0);
    expect("", "", EMPTY512 " *-\n", 0);
    expect("-l 0", "abc", ABC512 " *-\n", 0);                            // 0 is the whole 512 bits
    expect("-l 64", "abc", "d8bb14d833d59559 *-\n", 0);
    expect("--length=128", "", "cae66941d9efbd404e4d88758ea67670 *-\n", 0);
    expect("-l 8 -t", "abc", "6b  -\n", 0);                              // text mode: a blank instead of the star
    expect("--tag -l 64", "abc", "BLAKE2b-64 (-) = d8bb14d833d59559\n", 0);
    expect("--tag", "abc", "BLAKE2b (-) = " ABC512 "\n", 0);

    printf("files and checking\n");
    write_text(workDir, "abc.txt", "abc");
    expect("abc.txt", "", ABC512 " *abc.txt\n", 0);
    expect("-l 64 abc.txt abc.txt", "", "d8bb14d833d59559 *abc.txt\nd8bb14d833d59559 *abc.txt\n", 0);
    write_text(workDir, "sums.txt", ABC512 " *abc.txt\n");
    expect("-c sums.txt", "", "abc.txt: OK\n", 0);
    write_text(workDir, "short.txt", "d8bb14d833d59559 *abc.txt\n");           // the length follows the line
    expect("-c short.txt", "", "abc.txt: OK\n", 0);
    write_text(workDir, "tag.txt", "BLAKE2b-64 (abc.txt) = d8bb14d833d59559\n");
    expect("-c tag.txt", "", "abc.txt: OK\n", 0);
    write_text(workDir, "tag512.txt", "BLAKE2b (abc.txt) = " ABC512 "\n");
    expect("-c tag512.txt", "", "abc.txt: OK\n", 0);
    write_text(workDir, "bad.txt", "d8bb14d833d59558 *abc.txt\n");
    expect("-c bad.txt", "", "abc.txt: FAILED\n", 1);
    write_text(workDir, "single.txt", "d8bb14d833d59559 abc.txt\n");           // one blank before the name
    expect("-c single.txt", "", "abc.txt: OK\n", 0);
    write_text(workDir, "odd.txt", "d8bb14d833d595590 *abc.txt\n");           // an odd number of digits
    expect("-c odd.txt", "", "", 1);
    write_text(workDir, "wrong.txt", "BLAKE2b-64 (abc.txt) = " ABC512 "\n");   // the digits do not fit the bits
    expect("-c wrong.txt", "", "", 1);

    printf("errors\n");
    expect("-l 7 abc.txt", "", "", 1);
    expect("-l 520 abc.txt", "", "", 1);
    expect("-l x abc.txt", "", "", 1);
    expect("--tag -c sums.txt", "", "", 1);
    expect("nosuchfile", "", "", 1);
    expect("--version", "", "b2sum (Winshell) 0.1\n", 0);

    return test_summary("b2sum_test");
}
