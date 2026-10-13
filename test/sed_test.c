/*
 * Tests of sed, run as the program it is. The comparison with GNU sed (gnu_compare.sh) covers the
 * behaviour in bulk; these are the things only this project does or that need files: CR LF lines,
 * -i with a backup, w and r files, virtual paths, UTF-8, exit statuses.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void test_substitute(void) {
    printf("s\n");

    text_expect("sed", "\"s/a/X/\"", "banana\n", "bXnana\n");
    text_expect("sed", "\"s/a/X/g\"", "banana\n", "bXnXnX\n");
    text_expect("sed", "\"s/a/X/2\"", "banana\n", "banXna\n");
    text_expect("sed", "\"s/a/X/2g\"", "banana\n", "banXnX\n");
    text_expect("sed", "-n \"s/a/X/p\"", "banana\nxyz\n", "bXnana\n");
    text_expect("sed", "\"s/\\(b\\)\\(a\\)/\\2\\1/\"", "banana\n", "abnana\n");
    text_expect("sed", "-E \"s/(b)(a)/\\2\\1&/\"", "banana\n", "abbanana\n");
    text_expect("sed", "\"s/a/[&]/g\"", "aa\n", "[a][a]\n");
    text_expect("sed", "\"s/a/\\&/\"", "a\n", "&\n");
    text_expect("sed", "\"s|/|-|g\"", "a/b/c\n", "a-b-c\n");
    text_expect("sed", "\"s/b*/X/g\"", "abc\n", "XaXcX\n");
    text_expect("sed", "\"s/hello/\\U&/\"", "hello world\n", "HELLO world\n");
    text_expect("sed", "\"s/\\w\\+/\\u&/g\"", "foo bar\n", "Foo Bar\n");
    text_expect("sed", "\"s/\\(.*\\)/\\L\\1/\"", "ABC\n", "abc\n");
    text_expect("sed", "\"s/a/b/I\"", "A\n", "b\n");
    text_expect("sed", "\"s/x/\\n/\"", "axb\n", "a\nb\n");

    // groups under repetition and alternation
    text_expect("sed", "\"s/\\(a\\|b\\)*c/<\\1>/\"", "abbc\n", "<b>\n");
    text_expect("sed", "-E \"s/(a+)(b*)/\\2\\1/\"", "aabbb\n", "bbbaa\n");
}

static void test_addresses(void) {
    printf("addresses\n");

    text_expect("sed", "-n 2p", "1\n2\n3\n", "2\n");
    text_expect("sed", "-n $p", "1\n2\n3\n", "3\n");
    text_expect("sed", "-n 2,3p", "1\n2\n3\n4\n", "2\n3\n");
    text_expect("sed", "-n 2,+1p", "1\n2\n3\n4\n", "2\n3\n");
    text_expect("sed", "-n 1~2p", "1\n2\n3\n4\n5\n", "1\n3\n5\n");
    text_expect("sed", "-n \"/b/,/c/p\"", "a\nb\nx\nc\nd\n", "b\nx\nc\n");
    text_expect("sed", "-n \"/b/!p\"", "a\nb\nc\n", "a\nc\n");
    text_expect("sed", "\"0,/a/d\"", "a\na\nb\n", "a\nb\n");
    text_expect("sed", "\"1,/a/d\"", "a\na\nb\n", "b\n");
    text_expect("sed", "-n \"\\,b,p\"", "a\nb\n", "b\n");
    text_expect("sed", "2d", "1\n2\n3\n", "1\n3\n");
    text_expect("sed", "\"2,3c X\"", "1\n2\n3\n4\n", "1\nX\n4\n");
}

static void test_commands(void) {
    printf("commands\n");

    text_expect("sed", "\"a foo\"", "1\n2\n", "1\nfoo\n2\nfoo\n");
    text_expect("sed", "\"2i bar\"", "1\n2\n", "1\nbar\n2\n");
    text_expect("sed", "-n =", "a\nb\n", "1\n2\n");
    text_expect("sed", "\"y/abc/xyz/\"", "aabbcc\n", "xxyyzz\n");
    text_expect("sed", "-n l", "a\tb\\\n", "a\\tb\\\\$\n");
    text_expect("sed", "-n l", "\xC3\xA9\n", "\\303\\251$\n");
    text_expect("sed", "\"1!G;h;$!d\"", "1\n2\n3\n", "3\n2\n1\n");
    text_expect("sed", "\"N;P;D\"", "1\n2\n3\n", "1\n2\n3\n");
    text_expect("sed", "\":a;N;$!ba;s/\\n/,/g\"", "1\n2\n3\n", "1,2,3\n");
    text_expect("sed", "\":a;s/aa/a/;ta\"", "aaaa\n", "a\n");
    text_expect("sed", "\"s/a/b/;T;s/$/!/\"", "a\nc\n", "b!\nc\n");
    text_expect("sed", "\"x;G\"", "a\nb\n", "\na\na\nb\n");
    text_expect("sed", "2q", "1\n2\n3\n", "1\n2\n");
    text_expect("sed", "2Q", "1\n2\n3\n", "1\n");
    text_expect("sed", "\"n;d\"", "1\n2\n3\n", "1\n3\n");
    text_expect("sed", "-z \"s/\\n/,/g\"", "a\nb\n", "a,b,");
    text_expect("sed", "-n \"#n\"", "a\n", "");
    text_expect("sed", "\"#n\np\"", "a\n", "a\n");
    text_expect("sed", "-e \"a\\\\\" -e text", "1\n", "1\ntext\n");
}

static void test_status(void) {
    RunResult r;

    printf("exit status\n");

    run_tool(workDir, "sed", "3q5", "1\n2\n3\n4\n", &r);
    CHECK_INT(r.exitCode, 5);
    CHECK_STR(r.out, "1\n2\n3\n");
    run_result_free(&r);

    run_tool(workDir, "sed", "\"s/a/b\"", "x\n", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "unterminated") != NULL);
    run_result_free(&r);

    run_tool(workDir, "sed", "k", "x\n", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    run_tool(workDir, "sed", "\"b nowhere\"", "x\n", &r);
    CHECK_INT(r.exitCode, 4);
    run_result_free(&r);

    // a missing input file is reported, the others are read, and the status is 2
    write_text(workDir, "sed_in.txt", "in\n");
    run_tool(workDir, "sed", "p nosuch.txt sed_in.txt", "", &r);
    CHECK_INT(r.exitCode, 2);
    CHECK_STR(r.out, "in\nin\n");
    CHECK(strstr(r.err, "nosuch.txt") != NULL);
    run_result_free(&r);

    run_tool(workDir, "sed", "", "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
}

static void test_line_endings(void) {
    printf("line endings\n");

    // CR LF is a line ending, not text: $ is before it and it is written back
    text_expect("sed", "\"s/one$/X/\"", "one\r\ntwo\r\n", "X\r\ntwo\r\n");
    text_expect("sed", "-n $p", "a\r\nb\r\n", "b\r\n");
    text_expect("sed", "\"s/$/!/\"", "a\r\nb\n", "a!\r\nb!\n");
    text_expect("sed", "\"N;s/\\n/+/\"", "a\r\nb\r\n", "a+b\r\n");

    // a last line without ending stays without one; what comes after it makes up for it
    text_expect("sed", "p", "a\nb", "a\na\nb\nb");
    text_expect("sed", "\"s/b/c/\"", "a\nb", "a\nc");
    text_expect("sed", "\"$a end\"", "a\nb", "a\nb\nend\n");
    text_expect("sed", "G", "a", "a\n\n");
    text_expect("sed", "p", "", "");
}

static void test_files(void) {
    RunResult r;
    char arguments[MAX_PATH * 3];

    printf("files\n");

    // -i, with and without a backup
    write_text(workDir, "ed1.txt", "one\ntwo\n");
    run_tool(workDir, "sed", "-i \"s/one/1/\" ed1.txt", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK_STR(r.out, "");
    run_result_free(&r);
    expect_content(workDir, "ed1.txt", "1\ntwo\n");

    write_text(workDir, "ed2.txt", "a\nb\n");
    run_tool(workDir, "sed", "-i.bak \"s/a/A/\" ed2.txt", "", &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    expect_content(workDir, "ed2.txt", "A\nb\n");
    expect_content(workDir, "ed2.txt.bak", "a\nb\n");

    write_text(workDir, "ed3.txt", "x\ny\n");
    run_tool(workDir, "sed", "--in-place=orig_* \"s/x/X/\" ed3.txt", "", &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    expect_content(workDir, "ed3.txt", "X\ny\n");
    expect_content(workDir, "orig_ed3.txt", "x\ny\n");

    // -i with two files: each is its own stream ($ and line numbers)
    write_text(workDir, "ed4.txt", "1\n2\n");
    write_text(workDir, "ed5.txt", "3\n4\n");
    run_tool(workDir, "sed", "-i \"1d;$s/$/!/\" ed4.txt ed5.txt", "", &r);
    run_result_free(&r);
    expect_content(workDir, "ed4.txt", "2!\n");
    expect_content(workDir, "ed5.txt", "4!\n");

    // -i and q keep the lines up to it
    write_text(workDir, "ed6.txt", "1\n2\n3\n");
    run_tool(workDir, "sed", "-i 2q ed6.txt", "", &r);
    run_result_free(&r);
    expect_content(workDir, "ed6.txt", "1\n2\n");

    run_tool(workDir, "sed", "-i p", "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    // -s: $ and the line numbers per file
    write_text(workDir, "s1.txt", "a\nb\n");
    write_text(workDir, "s2.txt", "c\nd\n");
    run_tool(workDir, "sed", "-n $p s1.txt s2.txt", "", &r);
    CHECK_STR(r.out, "d\n");
    run_result_free(&r);
    run_tool(workDir, "sed", "-s -n $p s1.txt s2.txt", "", &r);
    CHECK_STR(r.out, "b\nd\n");
    run_result_free(&r);
    run_tool(workDir, "sed", "-s -n 1F s1.txt s2.txt", "", &r);
    CHECK_STR(r.out, "s1.txt\ns2.txt\n");
    run_result_free(&r);
    run_tool(workDir, "sed", "-n 1p - s1.txt", "x\n", &r);
    CHECK_STR(r.out, "x\n");
    run_result_free(&r);

    // w, r and R
    run_tool(workDir, "sed", "-n \"/b/w sed_w.txt\" s1.txt s2.txt", "", &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    expect_content(workDir, "sed_w.txt", "b\n");
    run_tool(workDir, "sed", "\"1r s2.txt\" s1.txt", "", &r);
    CHECK_STR(r.out, "a\nc\nd\nb\n");
    run_result_free(&r);
    run_tool(workDir, "sed", "\"R s2.txt\" s1.txt", "", &r);
    CHECK_STR(r.out, "a\nc\nb\nd\n");
    run_result_free(&r);
    run_tool(workDir, "sed", "\"s/a/b/w sed_w2.txt\" s1.txt", "", &r);
    run_result_free(&r);
    expect_content(workDir, "sed_w2.txt", "b\n");
    run_tool(workDir, "sed", "-n \"p;w /dev/stdout\" s1.txt", "", &r);
    CHECK_STR(r.out, "a\na\nb\nb\n");
    run_result_free(&r);

    // a script file, and virtual paths
    write_text(workDir, "script.sed", "# comment\ns/a/A/\n/b/{\ns/b/B/\n}\n");
    run_tool(workDir, "sed", "-f script.sed s1.txt", "", &r);
    CHECK_STR(r.out, "A\nB\n");
    run_result_free(&r);
    snprintf(arguments, sizeof arguments, "-f %s/script.sed %s/s1.txt", virtualWorkDir, virtualWorkDir);
    run_tool(workDir, "sed", arguments, "", &r);
    CHECK_STR(r.out, "A\nB\n");
    run_result_free(&r);
    run_tool(workDir, "sed", "-f nosuch.sed s1.txt", "", &r);
    CHECK_INT(r.exitCode, 4);
    run_result_free(&r);

    // a directory is not a file to edit
    make_directory("sed_dir");
    run_tool(workDir, "sed", "p sed_dir", "", &r);
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);
}

static void test_utf8(void) {
    printf("UTF-8\n");

    text_expect("sed", "\"s/./X/\"", "가나다\n", "X나다\n");
    text_expect("sed", "\"s/./X/g\"", "가나다\n", "XXX\n");
    text_expect("sed", "\"y/가나/AB/\"", "가나다\n", "AB다\n");
    text_expect("sed", "\"s/[가-나]/_/g\"", "가나다\n", "__다\n");
    text_expect("sed", "\"s/.*/\\U&/\"", "école\n", "ÉCOLE\n");
    text_expect("sed", "\"s/\\(.\\)\\(.\\)/\\2\\1/\"", "가나다\n", "나가다\n");
}

int main(void) {
    tools_init("sed_test");
    test_substitute();
    test_addresses();
    test_commands();
    test_status();
    test_line_endings();
    test_files();
    test_utf8();
    return test_summary("sed_test");
}
