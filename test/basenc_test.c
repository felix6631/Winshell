/*
 * Tests of basenc, run as the program it is: every encoding encoded and decoded (the vectors of RFC 4648 and Z85), the
 * lines of 76 and -w, decoding with its errors (what is written before the error) and -i, and the checks of the options.
 * GNU basenc is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("basenc", arguments, input, expected, status);
}

static void message(const char* arguments, const char* expectedError, int status) {
    RunResult r;

    run_tool(workDir, "basenc", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    lf_only(r.err);
    if (strcmp(r.err, expectedError) != 0)
        printf("    for: basenc %s\n    got: \"%s\"\n", arguments, r.err);
    CHECK_STR(r.err, expectedError);
    run_result_free(&r);
}

int main(void) {
    tools_init("basenc_test");
    reset_dir(workDir);

    printf("base64 and base32\n");
    expect("--base64", "foobar", "Zm9vYmFy\n", 0);
    expect("--base64", "fo", "Zm8=\n", 0);
    expect("--base64 -d", "Zm9vYmFy\n", "foobar", 0);
    expect("--base32", "foobar", "MZXW6YTBOI======\n", 0);
    expect("--base32 -d", "MZXW6YTBOI======\n", "foobar", 0);
    expect("--base64url", "\xff\xfe\xfd", "__79\n", 0);
    expect("--base64url -d", "__79\n", "\xff\xfe\xfd", 0);
    expect("--base64url", "hello?", "aGVsbG8_\n", 0);
    expect("--base32hex", "foobar", "CPNMUOJ1E8======\n", 0);
    expect("--base32hex -d", "CPNMUOJ1E8======\n", "foobar", 0);

    printf("base16, bit strings and Z85\n");
    expect("--base16", "foobar", "666F6F626172\n", 0);
    expect("--base16 -d", "666F6F626172\n", "foobar", 0);
    expect("--base2msbf", "A", "01000001\n", 0);
    expect("--base2lsbf", "A", "10000010\n", 0);
    expect("--base2msbf -d", "01000001", "A", 0);
    expect("--base2lsbf -d", "10000010", "A", 0);
    expect("--base2msbf -w 4", "A", "0100\n0001\n", 0);
    expect("--z85", "abcd", "vpA.S\n", 0);
    expect("--z85 -d", "vpA.S\n", "abcd", 0);
    expect("--z85 -d", "HelloWorld", "\x86O\xd2o\xb5Y\xf7[", 0);               // the example of the Z85 specification
    expect("--z85 -w 4", "abcdefgh", "vpA.\nSwOb\nN*\n", 0);
    expect("--base16 -w 0", "abc", "616263", 0);
    expect("--base16 -w 4", "abc", "6162\n63\n", 0);
    expect("--base16", "", "", 0);

    printf("invalid input: what is decoded before the error is written\n");
    expect("--base16 -d", "48656c6c6f", "He", 1);                                // only upper case digits
    expect("--base16 -d", "4865F", "He", 1);
    expect("--base16 -d", "48GG", "H", 1);
    expect("--base16 -d", "4865\n6C", "Hel", 0);                                 // newlines are skipped
    expect("--base16 -d", "48 65", "H", 1);
    expect("--base16 -di", "48 65", "He", 0);
    expect("--base16 -di", "48zz65", "He", 0);
    expect("--base16 -d", "", "", 0);
    expect("--base2msbf -d", "0100000", "", 1);
    expect("--base2msbf -d", "010000011", "A", 1);
    expect("--base2lsbf -d", "0100", "", 1);
    expect("--base2msbf -d", "0102", "", 1);
    expect("--base2msbf -di", "01000001x", "A", 0);
    expect("--base2msbf -d", "0100\n0001", "A", 0);
    expect("--z85 -d", "Hello", "\x86O\xd2o", 0);
    expect("--z85 -d", "Hell", "", 1);
    expect("--z85 -d", "Hell~", "", 1);
    expect("--z85 -d", "", "", 0);
    expect("--base64 -d", "Zm9v!", "foo", 1);

    printf("the options\n");
    message("", "basenc: missing encoding type\nTry 'basenc --help' for more information.\n", 1);
    message("--base64 -w x", "basenc: invalid wrap size: 'x'\n", 1);
    message("--base64 a b", "basenc: extra operand 'b'\nTry 'basenc --help' for more information.\n", 1);
    message("--base64 nosuchfile_zz", "basenc: nosuchfile_zz: No such file or directory\n", 1);
    message("--z85 -d x", "basenc: x: No such file or directory\n", 1);
    expect_run("basenc", "--version", "", "basenc (Winshell) 0.1\n", 0);
    return test_summary("basenc_test");
}
