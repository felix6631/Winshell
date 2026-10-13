/*
 * Tests of grep as a program: what the GNU comparison cannot say because the behaviour is
 * deliberately different (CR LF endings are not part of a line), or that needs a running process
 * (output that is passed on as it comes), or files that are made here (-r, -f).
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

#define WAIT 5000

static void test_line_endings(void) {
    RunResult r;

    printf("line endings\n");

    // a CR in front of the line feed is not part of the line: $ matches before it, and it is written back
    text_expect("grep", "foo$", "foo\r\nbar\r\nfoo bar\r\n", "foo\r\n");
    text_expect("grep", "^$", "a\r\n\r\nb\r\n", "\r\n");
    text_expect("grep", "-c ^$", "a\r\n\r\nb\r\n", "1\n");
    text_expect("grep", "-x foo", "foo\r\nfoo\n", "foo\r\nfoo\n");
    text_expect("grep", "-o o", "foo\r\n", "o\no\n");
    text_expect("grep", "-n bar", "foo\r\nbar\r\n", "2:bar\r\n");
    text_expect("grep", "-v foo", "foo\r\nbar\r\n", "bar\r\n");
    text_expect("grep", "-A1 foo", "foo\r\nbar\r\nbaz\r\n", "foo\r\nbar\r\n");

    // the last line has an ending when it is written, which is a line feed
    text_expect("grep", "bar", "foo\nbar", "bar\n");
    text_expect("grep", "-c bar", "foo\nbar", "1\n");
    run_tool(workDir, "grep", "foo", "", &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    // a pattern file with CR LF endings gives the same patterns
    write_text(workDir, "grep_crlf_patterns.txt", "foo\r\nbar\r\n");
    text_expect("grep", "-f grep_crlf_patterns.txt", "foo\nbaz\nbar\n", "foo\nbar\n");

    // with -z the lines end at NUL and nothing is done about CR
    run_tool(workDir, "grep", "-z a", "a\nb\0c\0", &r);
    CHECK_INT((int)r.outLength, 4);
    CHECK(r.outLength == 4 && memcmp(r.out, "a\nb\0", 4) == 0);
    run_result_free(&r);
}

static void test_utf8_and_text(void) {
    RunResult r;

    printf("UTF-8 and binary data\n");

    // a character is one character
    text_expect("grep", "-c ^.$", "\xEA\xB0\x80\nab\nx\n", "2\n");
    text_expect("grep", "-o [\xEA\xB0\x80-\xED\x9E\xA3]", "ab\xEB\x82\x98\xEB\x8B\xA4z\n", "\xEB\x82\x98\n\xEB\x8B\xA4\n");
    text_expect("grep", "-io \xC3\xA9", "CAF\xC3\x89 caf\xC3\xA9\n", "\xC3\x89\n\xC3\xA9\n");

    // a file with a NUL is binary: the lines are not shown
    {
        char path[MAX_PATH * 2];
        size_t length = 0;
        FILE* f;

        snprintf(path, sizeof path, "%s\\grep_binary.dat", workDir);
        f = fopen(path, "wb");
        CHECK(f != NULL);
        if (f != NULL) {
            fwrite("one\0two\nthree\n", 1, 15, f);
            fclose(f);
        }
        (void)length;
    }
    run_tool(workDir, "grep", "t grep_binary.dat", NULL, &r);
    CHECK_STR(lf_only(r.out), "Binary file grep_binary.dat matches\n");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    run_tool(workDir, "grep", "-c t grep_binary.dat", NULL, &r);
    CHECK_STR(lf_only(r.out), "2\n");
    run_result_free(&r);
    run_tool(workDir, "grep", "-a three grep_binary.dat", NULL, &r);
    CHECK_STR(lf_only(r.out), "three\n");
    run_result_free(&r);
    run_tool(workDir, "grep", "-I t grep_binary.dat", NULL, &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
}

static void test_files(void) {
    RunResult r;
    char dir[MAX_PATH * 2];

    printf("files and directories\n");

    snprintf(dir, sizeof dir, "%s\\grep_tree", workDir);
    CreateDirectoryA(dir, NULL);
    snprintf(dir, sizeof dir, "%s\\grep_tree\\sub", workDir);
    CreateDirectoryA(dir, NULL);
    snprintf(dir, sizeof dir, "%s\\grep_tree", workDir);
    write_text(dir, "a.txt", "needle one\nhay\n");
    write_text(dir, "b.c", "needle two\n");
    snprintf(dir, sizeof dir, "%s\\grep_tree\\sub", workDir);
    write_text(dir, "c.txt", "deep needle\n");

    // -r with no file searches the current directory, and names have no "./" in front
    {
        char cwd[MAX_PATH * 2];
        char path[MAX_PATH * 2];
        snprintf(cwd, sizeof cwd, "%s\\grep_tree", workDir);
        bin_path("grep", path, sizeof path);
        CHECK(run_program(path, "-r needle", NULL, cwd, &r) == 0);
        CHECK_STR(lf_only(r.out), "a.txt:needle one\nb.c:needle two\nsub/c.txt:deep needle\n");
        run_result_free(&r);

        CHECK(run_program(path, "-r -l needle .", NULL, cwd, &r) == 0);
        CHECK_STR(lf_only(r.out), "./a.txt\n./b.c\n./sub/c.txt\n");
        run_result_free(&r);

        CHECK(run_program(path, "-rc needle --include=*.txt", NULL, cwd, &r) == 0);
        CHECK_STR(lf_only(r.out), "a.txt:1\nsub/c.txt:1\n");
        run_result_free(&r);

        CHECK(run_program(path, "-r needle --exclude-dir=sub --exclude=*.c", NULL, cwd, &r) == 0);
        CHECK_STR(lf_only(r.out), "a.txt:needle one\n");
        run_result_free(&r);
    }

    // a directory is an error without -r
    run_tool(workDir, "grep", "needle grep_tree", NULL, &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "grep_tree: Is a directory") != NULL);
    run_result_free(&r);

    // virtual paths, and several files give names
    {
        char arguments[MAX_PATH * 3];
        snprintf(arguments, sizeof arguments, "needle %s/grep_tree/a.txt %s/grep_tree/b.c", virtualWorkDir,
                 virtualWorkDir);
        run_tool(workDir, "grep", arguments, NULL, &r);
        CHECK(strstr(r.out, "a.txt:needle one") != NULL && strstr(r.out, "b.c:needle two") != NULL);
        run_result_free(&r);
    }

    // standard input has a name when it must have one
    text_expect("grep", "-H x", "x\n", "(standard input):x\n");
    text_expect("grep", "-c x - ", "x\nx\n", "2\n");
    text_expect("grep", "--label=in -H x", "x\n", "in:x\n");

    // exit status: 0 for a selected line, 1 for none, 2 for an error, and -q is quiet
    run_tool(workDir, "grep", "-q x", "x\n", &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    run_tool(workDir, "grep", "-q y", "x\n", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
    run_tool(workDir, "grep", "x nosuchfile_here.txt", NULL, &r);
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "nosuchfile_here.txt: No such file or directory") != NULL);
    run_result_free(&r);
    run_tool(workDir, "grep", "-s x nosuchfile_here.txt", NULL, &r);
    CHECK_INT(r.exitCode, 2);
    CHECK_STR(r.err, "");
    run_result_free(&r);
    run_tool(workDir, "grep", "-q x nosuchfile_here.txt grep_tree/a.txt", NULL, &r);
    CHECK_INT(r.exitCode, 2);           // the error comes first, and nothing was selected yet
    run_result_free(&r);
    run_tool(workDir, "grep", "'(' -E", "x\n", &r);
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);
    run_tool(workDir, "grep", "", NULL, &r);
    CHECK_INT(r.exitCode, 2);           // no pattern: usage
    run_result_free(&r);
}

static void test_streaming(void) {
    Process p;
    RunResult r;
    char path[MAX_PATH * 2];

    printf("output as it comes\n");

    bin_path("grep", path, sizeof path);
    CHECK(proc_start(&p, path, "--line-buffered a", workDir) == 0);
    CHECK(proc_write(&p, "bad\n", 4) == 0);
    CHECK(proc_wait_for_output(&p, "bad", WAIT));
    CHECK(!proc_finished(&p));
    CHECK(proc_write(&p, "no\ncat\r\n", 8) == 0);
    CHECK(proc_wait_for_output(&p, "cat", WAIT));
    proc_close_input(&p);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_STR(r.out, "bad\ncat\r\n");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    // -m ends the program when it has what it asked for, without waiting for the end of the input
    CHECK(proc_start(&p, path, "-m1 x", workDir) == 0);
    CHECK(proc_write(&p, "x\n", 2) == 0);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_STR(r.out, "x\n");
    run_result_free(&r);

    // so does -q
    CHECK(proc_start(&p, path, "-q x", workDir) == 0);
    CHECK(proc_write(&p, "x\n", 2) == 0);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
}

static void test_context(void) {
    printf("context\n");

    text_expect("grep", "-n -C1 c", "a\nb\nc\nd\ne\n", "2-b\n3:c\n4-d\n");
    text_expect("grep", "-A1 a", "a\nb\nc\nd\na\nx\n", "a\nb\n--\na\nx\n");
    text_expect("grep", "-A1 a", "a\nb\na\nc\nd\n", "a\nb\na\nc\n");           // the groups join: no separator
    text_expect("grep", "-B2 d", "a\nb\nc\nd\n", "b\nc\nd\n");
    text_expect("grep", "-c -A5 a", "a\nb\n", "1\n");
    text_expect("grep", "-m1 -A1 a", "a\nb\na\n", "a\nb\n");
    text_expect("grep", "-o -A1 a", "a\nb\n", "a\n");
    text_expect("grep", "-2 c", "a\nb\nc\nd\ne\nf\n", "a\nb\nc\nd\ne\n");
}

int main(void) {
    tools_init("grep_test");

    test_line_endings();
    test_utf8_and_text();
    test_files();
    test_context();
    test_streaming();
    return test_summary("grep_test");
}
