/*
 * Tests of diff, run as the program it is: the three formats, ignoring options, line endings, a last
 * line without its line feed, standard input, directories, large inputs and the exit status.
 * What GNU diff prints for a great many combinations is compared by gnu_compare.sh (section diff).
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("diff", arguments, input, expected, status);
}

static void test_formats(void) {
    printf("normal, unified and context formats\n");
    write_text(workDir, "a.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");
    write_text(workDir, "b.txt", "1\n2\nthree\n4\n5\n6\n7\n8\n9\n10\n11\n");

    expect("a.txt a.txt", "", "", 0);
    expect("-s a.txt a.txt", "", "Files a.txt and a.txt are identical\n", 0);
    expect("a.txt b.txt", "", "3c3\n< 3\n---\n> three\n10a11\n> 11\n", 1);
    expect("-q a.txt b.txt", "", "Files a.txt and b.txt differ\n", 1);
    expect("-u --label A --label B a.txt b.txt", "",
           "--- A\n+++ B\n@@ -1,6 +1,6 @@\n 1\n 2\n-3\n+three\n 4\n 5\n 6\n@@ -8,3 +8,4 @@\n 8\n 9\n 10\n+11\n", 1);
    expect("-U0 --label A --label B a.txt b.txt", "",
           "--- A\n+++ B\n@@ -3 +3 @@\n-3\n+three\n@@ -10,0 +11 @@\n+11\n", 1);
    expect("-c --label A --label B a.txt b.txt", "",
           "*** A\n--- B\n***************\n*** 1,6 ****\n  1\n  2\n! 3\n  4\n  5\n  6\n--- 1,6 ----\n  1\n  2\n! three\n"
           "  4\n  5\n  6\n***************\n*** 8,10 ****\n--- 8,11 ----\n  8\n  9\n  10\n+ 11\n", 1);

    // the unified heading has the modification time of the file when there is no label
    {
        RunResult r;
        run_tool(workDir, "diff", "-u a.txt b.txt", "", &r);
        CHECK(strncmp(r.out, "--- a.txt\t", 10) == 0);
        CHECK(strstr(r.out, "\n+++ b.txt\t") != NULL);
        CHECK(r.out[10] >= '0' && r.out[10] <= '9');
        run_result_free(&r);
    }
}

static void test_inputs(void) {
    printf("ignored differences, line endings, missing line feeds, standard input\n");
    write_text(workDir, "case1.txt", "Hello\nworld\n");
    write_text(workDir, "case2.txt", "hello\nWORLD\n");
    expect("-i case1.txt case2.txt", "", "", 0);
    expect("case1.txt case2.txt", "", "1,2c1,2\n< Hello\n< world\n---\n> hello\n> WORLD\n", 1);

    write_text(workDir, "sp1.txt", "a  b\nc\n\nd\n");
    write_text(workDir, "sp2.txt", "a b\nc  \n\n\nd\n");
    expect("-b sp1.txt sp2.txt", "", "2a3\n> \n", 1);
    expect("-bB sp1.txt sp2.txt", "", "", 0);
    expect("-w sp1.txt sp2.txt", "", "2a3\n> \n", 1);

    write_text(workDir, "crlf.txt", "a\r\nb\r\n");
    write_text(workDir, "lf.txt", "a\nb\n");
    expect("crlf.txt lf.txt", "", "1,2c1,2\n< a\r\n< b\r\n---\n> a\n> b\n", 1);
    expect("--strip-trailing-cr crlf.txt lf.txt", "", "", 0);

    write_text(workDir, "nonl.txt", "x\ny");
    write_text(workDir, "nl.txt", "x\ny\n");
    expect("nonl.txt nl.txt", "", "2c2\n< y\n\\ No newline at end of file\n---\n> y\n", 1);

    expect("- nl.txt", "x\nz\n", "2c2\n< z\n---\n> y\n", 1);
    expect("nl.txt -", "x\nz\n", "2c2\n< y\n---\n> z\n", 1);
}

static void test_directories(void) {
    RunResult r;

    printf("directories\n");
    make_directory("d1");
    make_directory("d2");
    make_directory("d1\\sub");
    make_directory("d2\\sub");
    write_text(workDir, "d1\\only1.txt", "x\n");
    write_text(workDir, "d2\\only2.txt", "y\n");
    write_text(workDir, "d1\\same.txt", "s\n");
    write_text(workDir, "d2\\same.txt", "s\n");
    write_text(workDir, "d1\\sub\\f.txt", "a\nb\n");
    write_text(workDir, "d2\\sub\\f.txt", "a\nc\n");

    expect("d1 d2", "",
           "Only in d1: only1.txt\nOnly in d2: only2.txt\nCommon subdirectories: d1/sub and d2/sub\n", 1);
    expect("-r d1 d2", "",
           "Only in d1: only1.txt\nOnly in d2: only2.txt\ndiff -r d1/sub/f.txt d2/sub/f.txt\n2c2\n< b\n---\n> c\n", 1);
    expect("-rq d1 d2", "",
           "Only in d1: only1.txt\nOnly in d2: only2.txt\nFiles d1/sub/f.txt and d2/sub/f.txt differ\n", 1);
    expect("-r -x only* -x sub d1 d2", "", "", 0);
    expect("-rN d1 d2", "",
           "diff -rN d1/only1.txt d2/only1.txt\n1d0\n< x\ndiff -rN d1/only2.txt d2/only2.txt\n0a1\n> y\n"
           "diff -rN d1/sub/f.txt d2/sub/f.txt\n2c2\n< b\n---\n> c\n", 1);

    // a directory and a file: the file of that name in the directory
    write_text(workDir, "same.txt", "s\n");
    expect("d1 same.txt", "", "", 0);
    expect("same.txt d1", "", "", 0);

    printf("trouble\n");
    run_tool(workDir, "diff", "nosuch.txt same.txt", "", &r);
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "nosuch.txt: No such file or directory") != NULL);
    run_result_free(&r);
    run_tool(workDir, "diff", "same.txt", "", &r);
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);
    run_tool(workDir, "diff", "--bogus a b", "", &r);
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);
}

static void test_large(void) {
    RunResult r;
    char* one = malloc(20000 * 12);
    char* two = malloc(20000 * 12);
    size_t used1 = 0, used2 = 0;
    int i;

    printf("large inputs\n");
    for (i = 0; i < 20000; i++) {
        used1 += (size_t)sprintf(one + used1, "line %d\n", i);
        if (i % 1000 == 500)
            used2 += (size_t)sprintf(two + used2, "changed %d\n", i);
        else
            used2 += (size_t)sprintf(two + used2, "line %d\n", i);
    }
    write_text(workDir, "big1.txt", one);
    write_text(workDir, "big2.txt", two);
    run_tool(workDir, "diff", "-u --label a --label b big1.txt big2.txt", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.out, "@@ -498,7 +498,7 @@\n") != NULL);
    CHECK(strstr(r.out, "-line 19500\n+changed 19500\n") != NULL);
    run_result_free(&r);

    // nothing in common at all: still finishes
    for (i = 0; i < 3000; i++)
        used1 += 0;
    used1 = 0;
    used2 = 0;
    for (i = 0; i < 3000; i++) {
        used1 += (size_t)sprintf(one + used1, "left %d\n", i);
        used2 += (size_t)sprintf(two + used2, "right %d\n", i);
    }
    write_text(workDir, "big3.txt", one);
    write_text(workDir, "big4.txt", two);
    run_tool(workDir, "diff", "-q big3.txt big4.txt", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK_STR(r.out, "Files big3.txt and big4.txt differ\n");
    run_result_free(&r);
    free(one);
    free(two);
}

int main(void) {
    tools_init("diff_test");
    test_formats();
    test_inputs();
    test_directories();
    test_large();
    return test_summary("diff_test");
}
