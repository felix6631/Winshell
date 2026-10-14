/*
 * Tests of xargs, run as the program it is. The comparison with GNU xargs (gnu_compare.sh) covers
 * the batching in bulk; these are the exit statuses, files, UTF-8 and parallel runs written out.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* output, int status) {
    expect_run("xargs", arguments, input, output, status);
}

int main(void) {
    RunResult r;
    char arguments[MAX_PATH * 2];

    tools_init("xargs_test");

    printf("batches\n");
    expect("", "a b\nc\n", "a b c\n", 0);
    expect("-n 2 echo", "a b c d e\n", "a b\nc d\ne\n", 0);
    expect("-L 1 echo", "a b\nc d e\n", "a b\nc d e\n", 0);
    expect("-L 2 echo", "a b\nc\nd\n", "a b c\nd\n", 0);
    expect("-L 1 echo", "a \nb\nc\n", "a b\nc\n", 0);       // a trailing blank continues the line
    expect("-I {} echo [{}]", "a b\nc\n", "[a b]\n[c]\n", 0);
    expect("-I {} echo {}-{}", "x\n", "x-x\n", 0);
    expect("-s 12 echo", "aaaa bbbb cccc dddd\n", "aaaa\nbbbb\ncccc\ndddd\n", 0);
    expect("-E STOP echo", "a b\nSTOP\nc\n", "a b\n", 0);
    expect("echo hi", "", "hi\n", 0);                         // runs once without input
    expect("-r echo hi", "", "", 0);
    expect("-r echo hi", "  \n\n", "", 0);

    printf("items\n");
    expect("-n 1 echo", "a 'b c' \"d e\" f\\ g\n", "a\nb c\nd e\nf g\n", 0);
    expect("-0 -n 1 echo", "a b", "a b\n", 0);
    expect("-d , -n 1 echo", "a,b c,d\n", "a\nb c\nd\n\n", 0);   // the last item has the line feed
    expect("-n 1 echo", "가 나\n", "가\n나\n", 0);
    expect("-I {} echo <{}>", "한글 이름\n", "<한글 이름>\n", 0);
    expect("echo", "'unclosed\n", "\n", 1);          // like GNU xargs: the command is run once, without arguments

    printf("files\n");
    write_text(workDir, "xargs_in.txt", "one\ntwo\n");
    expect("-a xargs_in.txt -n 1 echo", "ignored\n", "one\ntwo\n", 0);
    snprintf(arguments, sizeof arguments, "-a %s/xargs_in.txt echo", virtualWorkDir);
    expect(arguments, "", "one two\n", 0);
    run_tool(workDir, "xargs", "-a nosuch.txt echo", "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    // the command gets its arguments, and cat shows that they are files
    write_text(workDir, "xa.txt", "AAA\n");
    write_text(workDir, "xb.txt", "BBB\n");
    expect("cat", "xa.txt xb.txt\n", "AAA\nBBB\n", 0);

    printf("exit status\n");
    expect("-n 1 cat", "nosuch1.txt xa.txt\n", "AAA\n", 123);
    expect("nosuchprogram_xyz", "a\n", "", 127);
    expect("--bogus", "", "", 1);
    expect("-s 1 echo", "abc\n", "", 1);
    expect("-n 0 echo", "a\n", "", 1);

    printf("parallel\n");
    run_tool(workDir, "xargs", "-P 3 -n 1 echo", "p1 p2 p3 p4 p5\n", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(has_line(r.out, "p1") && has_line(r.out, "p2") && has_line(r.out, "p3") && has_line(r.out, "p4") &&
          has_line(r.out, "p5"));
    run_result_free(&r);

    return test_summary("xargs_test");
}
