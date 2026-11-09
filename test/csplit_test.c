/*
 * Tests of csplit, run as the program it is: line numbers, regular expressions with offsets, skipping (%), repeats,
 * the options (prefix, digits, suffix format, -k, -s, -z, --suppress-matched) and the errors. The pieces are checked
 * by their contents. GNU csplit is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* expected, int status) {
    expect_run("csplit", arguments, "", expected, status);
}

static void fresh(void) {
    reset_dir(workDir);
    write_text(workDir, "n.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");
    write_text(workDir, "s.txt", "a\nb\nSTART x\nc\nSTART y\nd\ne\n");
}

int main(void) {
    tools_init("csplit_test");
    fresh();

    printf("line numbers\n");
    expect("n.txt 4", "6\n15\n", 0);
    expect_content(workDir, "xx00", "1\n2\n3\n");
    expect_content(workDir, "xx01", "4\n5\n6\n7\n8\n9\n10\n");
    fresh();
    expect("n.txt 3 6", "4\n6\n11\n", 0);
    expect_content(workDir, "xx01", "3\n4\n5\n");
    fresh();
    expect("n.txt 3 {2}", "4\n6\n6\n5\n", 0);                      // 3, 6 and 9: repeated
    expect_content(workDir, "xx03", "9\n10\n");
    fresh();
    expect("n.txt 11", "21\n", 1);                                         // out of range: nothing is kept
    CHECK(!path_exists_in(workDir, "xx00"));
    expect("-k n.txt 11", "21\n", 1);
    expect_content(workDir, "xx00", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");
    fresh();
    expect("n.txt 3 3", "4\n0\n17\n", 0);                              // the same line twice: an empty piece
    fresh();

    printf("regular expressions\n");
    expect("s.txt /START/ {*}", "4\n10\n12\n", 0);
    expect_content(workDir, "xx00", "a\nb\n");
    expect_content(workDir, "xx01", "START x\nc\n");
    expect_content(workDir, "xx02", "START y\nd\ne\n");
    fresh();
    expect("s.txt /START/+1", "12\n14\n", 0);                         // one line more in the first piece
    expect_content(workDir, "xx00", "a\nb\nSTART x\n");
    fresh();
    expect("s.txt /START/-1", "2\n24\n", 0);
    expect_content(workDir, "xx00", "a\n");
    fresh();
    expect("s.txt %START% /START/", "10\n12\n", 0);                    // skip to the first one
    expect_content(workDir, "xx00", "START x\nc\n");
    fresh();
    expect("s.txt /nomatch/", "26\n", 1);
    expect("s.txt /START/ {5}", "4\n10\n12\n", 1);
    expect("s.txt /START/ {*}", "4\n10\n12\n", 0);
    fresh();
    expect("s.txt /[/", "", 1);
    expect("s.txt abc", "", 1);
    expect("s.txt /START", "", 1);
    expect("s.txt /START/x", "", 1);
    expect("s.txt {2}", "", 1);
    expect("s.txt 0", "", 1);
    expect("s.txt 3 2", "", 1);
    expect("s.txt 2 {x}", "", 1);

    printf("--suppress-matched\n");
    expect("--suppress-matched s.txt /START/ {*}", "4\n2\n4\n", 0);
    expect_content(workDir, "xx01", "c\n");
    fresh();
    expect("--suppress-matched n.txt 4", "6\n13\n", 0);
    expect_content(workDir, "xx01", "5\n6\n7\n8\n9\n10\n");
    fresh();

    printf("names and options\n");
    expect("-f pp n.txt 4", "6\n15\n", 0);
    expect_content(workDir, "pp00", "1\n2\n3\n");
    expect("-n 1 -f pp n.txt 4", "6\n15\n", 0);
    expect_content(workDir, "pp0", "1\n2\n3\n");
    fresh();
    expect("-b -%d.txt -f pp n.txt 4 7", "6\n6\n9\n", 0);
    expect_content(workDir, "pp-1.txt", "4\n5\n6\n");
    fresh();
    expect("-s n.txt 4", "", 0);
    fresh();
    expect("-z n.txt 1 4", "6\n15\n", 0);                              // the empty first piece is removed
    expect_content(workDir, "xx00", "1\n2\n3\n");
    CHECK(!path_exists_in(workDir, "xx02"));
    fresh();
    expect("-b %c n.txt 4", "", 1);
    expect("-b abc n.txt 4", "", 1);
    expect("-b %d%d n.txt 4", "", 1);
    expect("-n x n.txt 4", "", 1);

    printf("input and operands\n");
    expect_run("csplit", "- 4", "1\n2\n3\n4\n5\n", "6\n4\n", 0);        // standard input
    expect_content(workDir, "xx00", "1\n2\n3\n");
    fresh();
    expect("nosuchfile 4", "", 1);
    expect("n.txt", "", 1);
    expect("", "", 1);
    expect("--bogus n.txt 4", "", 1);
    expect_run("csplit", "--version", "", "csplit (Winshell) 0.1\n", 0);
    fresh();

    return test_summary("csplit_test");
}
