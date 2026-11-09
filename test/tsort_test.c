/*
 * Tests of tsort, run as the program it is: the order of acyclic input, names split by blanks and lines, CR LF input,
 * what a loop does (reported on standard error, one pair dropped, status 1) and the errors.
 * GNU tsort is compared with by gnu_compare.sh (also on random graphs, loops included).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("tsort", arguments, input, expected, status);
}

int main(void) {
    RunResult r;

    tools_init("tsort_test");
    reset_dir(workDir);

    printf("order\n");
    expect("", "", "", 0);
    expect("", "a b\nb c\n", "a\nb\nc\n", 0);
    expect("", "c b\nb a\n", "c\nb\na\n", 0);
    expect("", "a b a c b d c d\n", "a\nc\nb\nd\n", 0);          // the pair read last is met first
    expect("", "b a\nd c\n", "b\nd\na\nc\n", 0);                   // the free names in the order of strcmp
    expect("", "a a\nb b\n", "a\nb\n", 0);                         // a name before itself says nothing
    expect("", "a b a b a b\n", "a\nb\n", 0);
    expect("", "  a\t b \n\n c\n\n  d  ", "a\nc\nb\nd\n", 0);      // names are split at blanks, tabs and line ends
    expect("", "10 9\n9 8\n", "10\n9\n8\n", 0);                    // names are text, not numbers
    expect("", "B a\n", "B\na\n", 0);

    printf("CR LF\n");
    expect("", "a b\r\nb c\r\n", "a\nb\nc\n", 0);

    printf("files\n");
    write_text(workDir, "g.txt", "x y\ny z\n");
    expect("g.txt", "", "x\ny\nz\n", 0);
    expect("-", "p q\n", "p\nq\n", 0);
    expect("g.txt g.txt", "", "", 1);
    expect("nosuchfile", "", "", 1);

    printf("loops\n");
    run_tool(workDir, "tsort", "", "a b b a\n", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK_STR(r.out, "a\nb\n");
    CHECK(strstr(r.err, "tsort: -: input contains a loop:") != NULL);
    run_result_free(&r);
    expect("", "a b b c c a\n", "a\nb\nc\n", 1);
    expect("", "a b b c c a d a\n", "d\na\nb\nc\n", 1);
    expect("", "a b b c c b\n", "a\nb\nc\n", 1);
    expect("", "a b b a c d d c\n", "a\nb\nc\nd\n", 1);

    printf("errors\n");
    run_tool(workDir, "tsort", "", "a b c\n", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK_STR(r.out, "");
    CHECK(strstr(r.err, "tsort: -: input contains an odd number of tokens") != NULL);
    run_result_free(&r);
    expect("--bogus", "", "", 1);
    expect("--version", "", "tsort (Winshell) 0.1\n", 0);

    return test_summary("tsort_test");
}
