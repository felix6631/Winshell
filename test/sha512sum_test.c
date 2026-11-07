/*
 * Tests of sha512sum, run as the program it is: the digests of the usual strings, the line formats (-b, -t, --tag, -z),
 * checking with -c and the errors. GNU sha512sum is compared with by gnu_compare.sh (all the *sum tools together).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("sha512sum", arguments, input, expected, status);
}

int main(void) {
    tools_init("sha512sum_test");
    reset_dir(workDir);

    printf("digests\n");
    expect("", "", "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e *-\n", 0);
    expect("", "abc", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f *-\n", 0);
    expect("", "The quick brown fox jumps over the lazy dog", "07e547d9586f6a73f73fbac0435ed76951218fb7d0c8d788a309d785436bbb642e93a252a954f23912547d1e8a3b5ed6e1bfd7097821233fa0538f3db854fee6 *-\n", 0);
    expect("-", "abc", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f *-\n", 0);

    printf("line formats\n");
    write_text(workDir, "abc.txt", "abc");
    expect("abc.txt", "", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f *abc.txt\n", 0);
    expect("-b abc.txt", "", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f *abc.txt\n", 0);
    expect("-t abc.txt", "", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f  abc.txt\n", 0);
    expect("--tag abc.txt", "", "SHA512 (abc.txt) = ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f\n", 0);
    expect("-z abc.txt", "", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f *abc.txt\0", 0);
    expect("abc.txt abc.txt", "", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f *abc.txt\nddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f *abc.txt\n", 0);

    printf("checking\n");
    write_text(workDir, "good.txt", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f *abc.txt\nddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f  abc.txt\n");
    expect("-c good.txt", "", "abc.txt: OK\nabc.txt: OK\n", 0);
    expect("-c --quiet good.txt", "", "", 0);
    expect("-c --status good.txt", "", "", 0);
    write_text(workDir, "bsd.txt", "SHA512 (abc.txt) = ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f\n");
    expect("-c bsd.txt", "", "abc.txt: OK\n", 0);
    write_text(workDir, "bad.txt", "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e *abc.txt\n");
    expect("-c bad.txt", "", "abc.txt: FAILED\n", 1);
    write_text(workDir, "gone.txt", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f *nosuchfile\n");
    expect("-c gone.txt", "", "nosuchfile: FAILED open or read\n", 1);
    expect("-c --ignore-missing gone.txt", "", "", 1);                  // nothing was verified
    write_text(workDir, "upper.txt", "DDAF35A193617ABACC417349AE20413112E6FA4E89A97EA20A9EEEE64B55D39A2192992A274FC1A836BA3C23A3FEEBBD454D4423643CE80E2A9AC94FA54CA49F *abc.txt\n");
    expect("-c upper.txt", "", "abc.txt: OK\n", 0);                    // the digits may be upper case
    write_text(workDir, "junk.txt", "junk\n");
    expect("-c junk.txt", "", "", 1);                                  // no line is a checksum
    write_text(workDir, "partly.txt", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f *abc.txt\njunk\n");
    expect("-c partly.txt", "", "abc.txt: OK\n", 0);
    expect("-c --strict partly.txt", "", "abc.txt: OK\n", 1);
    expect("-c", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f *abc.txt\n", "abc.txt: OK\n", 0);              // the list from standard input

    printf("errors\n");
    expect("nosuchfile", "", "", 1);
    expect("abc.txt nosuchfile", "", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f *abc.txt\n", 1);          // the others are still done
    expect("--tag -c good.txt", "", "", 1);
    expect("--status abc.txt", "", "", 1);                             // only for -c
    expect("-x", "", "", 1);
    expect("--version", "", "sha512sum (Winshell) 0.1\n", 0);

    return test_summary("sha512sum_test");
}
