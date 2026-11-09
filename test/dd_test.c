/*
 * Tests of dd, run as the program it is: block sizes and counts, skip and seek, the conversions (case, swab, sync,
 * block / unblock, ascii / ebcdic), copying to a file with seek / notrunc, the statistics on standard error and the errors.
 * GNU dd is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("dd", arguments, input, expected, status);
}

int main(void) {
    RunResult r;

    tools_init("dd_test");
    reset_dir(workDir);
    write_text(workDir, "abc.txt", "abcdefghijklmnopqrstuvwxyz");

    printf("blocks\n");
    expect("", "hello\n", "hello\n", 0);
    expect("if=abc.txt", "", "abcdefghijklmnopqrstuvwxyz", 0);
    expect("if=abc.txt bs=10 count=2", "", "abcdefghijklmnopqrst", 0);
    expect("if=abc.txt bs=10 skip=2", "", "uvwxyz", 0);
    expect("if=abc.txt bs=10 skip=1 count=1", "", "klmnopqrst", 0);
    expect("if=abc.txt ibs=4 obs=6 count=5", "", "abcdefghijklmnopqrst", 0);
    expect("if=abc.txt bs=2x3 count=1", "", "abcdef", 0);
    expect("if=abc.txt bs=1k count=0", "", "", 0);
    expect("if=abc.txt iflag=skip_bytes skip=5 bs=4 count=2", "", "fghijklm", 0);
    expect("if=abc.txt iflag=count_bytes count=7 bs=3", "", "abcdefg", 0);
    expect("if=abc.txt iflag=skip_bytes,count_bytes skip=2 count=5 bs=2", "", "cdefg", 0);
    expect("skip=1 bs=3", "abcdefghi", "defghi", 0);                      // standard input is read and dropped

    printf("conversions\n");
    expect("if=abc.txt conv=ucase", "", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0);
    expect("conv=lcase", "Hello WORLD", "hello world", 0);
    expect("conv=swab", "abcdef", "badcfe", 0);
    expect("conv=swab", "abcde", "badce", 0);                              // the last byte of an odd length stays
    expect("bs=3 conv=swab", "abcdefg", "badcfeg", 0);
    run_tool(workDir, "dd", "bs=4 conv=sync", "abcdef", &r);               // the short block is filled with NUL bytes
    CHECK_INT((int)r.outLength, 8);
    CHECK(memcmp(r.out, "abcdef\0\0", 8) == 0);
    run_result_free(&r);
    expect("conv=ascii", "", "", 0);
    expect("conv=ebcdic", "AB 1", "\xC1\xC2@\xF1", 0);
    expect("conv=ascii", "\xC1\xC2@\xF1", "AB 1", 0);
    expect("conv=block cbs=6", "ab\ncdefghi\n\nx", "ab    cdefgh      x     ", 0);
    expect("conv=unblock cbs=6", "ab    cdefgh      x     ", "ab\ncdefgh\n\nx\n", 0);
    expect("conv=ucase,lcase", "", "", 1);
    expect("conv=ascii,ebcdic", "", "", 1);
    expect("conv=foo", "", "", 1);
    expect("conv=", "", "", 1);

    printf("to a file\n");
    write_text(workDir, "out.txt", "0123456789");
    expect("if=abc.txt of=out.txt bs=5 count=1", "", "", 0);
    expect_content(workDir, "out.txt", "abcde");                          // the file is made shorter
    write_text(workDir, "out.txt", "0123456789");
    expect("if=abc.txt of=out.txt bs=5 count=1 conv=notrunc", "", "", 0);
    expect_content(workDir, "out.txt", "abcde56789");
    write_text(workDir, "out.txt", "0123456789");
    expect("if=abc.txt of=out.txt bs=2 count=2 seek=3 conv=notrunc", "", "", 0);
    expect_content(workDir, "out.txt", "012345abcd");
    write_text(workDir, "out.txt", "0123456789");
    expect("if=abc.txt of=out.txt bs=2 count=2 seek=3", "", "", 0);       // the file ends where the output ends
    expect_content(workDir, "out.txt", "012345abcd");
    write_text(workDir, "out.txt", "0123456789");
    expect("if=abc.txt of=out.txt bs=2 count=1 seek=1", "", "", 0);
    expect_content(workDir, "out.txt", "01ab");
    expect("if=abc.txt of=out.txt conv=excl", "", "", 1);                  // it exists
    expect("if=abc.txt of=nosuchdir/x", "", "", 1);

    printf("statistics and errors\n");
    run_tool(workDir, "dd", "if=abc.txt bs=10", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strstr(r.err, "2+1 records in") != NULL && strstr(r.err, "2+1 records out") != NULL);
    CHECK(strstr(r.err, "26 bytes copied") != NULL);
    run_result_free(&r);
    run_tool(workDir, "dd", "if=abc.txt bs=10 status=none", "", &r);
    CHECK_STR(r.err, "");
    run_result_free(&r);
    run_tool(workDir, "dd", "if=abc.txt bs=10 status=noxfer", "", &r);
    CHECK(strstr(r.err, "2+1 records in") != NULL && strstr(r.err, "copied") == NULL);
    run_result_free(&r);
    run_tool(workDir, "dd", "if=abc.txt bs=2k", "", &r);
    CHECK(strstr(r.err, "0+1 records in") != NULL);
    run_result_free(&r);
    expect("if=nosuchfile", "", "", 1);
    expect("bs=x", "", "", 1);
    expect("bs=0", "", "", 1);
    expect("count=-1", "", "", 1);
    expect("foo=bar", "", "", 1);
    expect("noequals", "", "", 1);
    expect("status=foo", "", "", 1);
    expect("iflag=foo", "", "", 1);
    expect("--version", "", "dd (Winshell) 0.1\n", 0);

    return test_summary("dd_test");
}
