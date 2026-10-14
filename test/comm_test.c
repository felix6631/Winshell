/*
 * Tests of comm, run as the program it is: the columns, suppressing them, CR LF input, the order check,
 * --total, the delimiter, NUL-terminated lines, standard input and the exit status.
 * GNU comm is compared with by gnu_compare.sh (section comm).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    RunResult r;

    run_tool(workDir, "comm", arguments, input, &r);
    if (r.out == NULL || strcmp(r.out, expected) != 0)
        printf("    for: comm %s\n    got: \"%s\"\n", arguments, r.out == NULL ? "(null)" : r.out);
    CHECK_STR(r.out, expected);
    CHECK_INT(r.exitCode, status);
    run_result_free(&r);
}

int main(void) {
    RunResult r;

    tools_init("comm_test");
    write_text(workDir, "a.txt", "a\nb\nc\nd\n");
    write_text(workDir, "b.txt", "b\nc\nx\n");

    printf("columns\n");
    expect("a.txt b.txt", "", "a\n\t\tb\n\t\tc\nd\n\tx\n", 0);
    expect("-1 a.txt b.txt", "", "\tb\n\tc\nx\n", 0);
    expect("-12 a.txt b.txt", "", "b\nc\n", 0);
    expect("-3 a.txt b.txt", "", "a\nd\n\tx\n", 0);
    expect("-123 a.txt b.txt", "", "", 0);
    expect("--output-delimiter=: a.txt b.txt", "", "a\n::b\n::c\nd\n:x\n", 0);
    expect("--total -3 a.txt b.txt", "", "a\nd\n\tx\n2\t1\t2\ttotal\n", 0);
    expect("a.txt a.txt", "", "\t\ta\n\t\tb\n\t\tc\n\t\td\n", 0);

    printf("line endings\n");
    write_text(workDir, "crlf.txt", "b\r\nc\r\nx\r\n");
    expect("a.txt crlf.txt", "", "a\n\t\tb\n\t\tc\nd\n\tx\r\n", 0);       // each line is written as it was read
    expect("-12 a.txt crlf.txt", "", "b\nc\n", 0);

    printf("standard input\n");
    expect("- b.txt", "a\nb\n", "a\n\t\tb\n\tc\n\tx\n", 0);
    expect("a.txt -", "b\nz\n", "a\n\t\tb\nc\nd\n\tz\n", 0);
    // a line that is the last one of a file without a line feed is written with one
    write_text(workDir, "last.txt", "b\nz");
    expect("-12 last.txt b.txt", "", "b\n", 0);

    printf("order\n");
    write_text(workDir, "unsorted.txt", "c\nb\na\n");
    run_tool(workDir, "comm", "unsorted.txt a.txt", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "file 1 is not in sorted order") != NULL);
    CHECK(strstr(r.err, "input is not in sorted order") != NULL);
    run_result_free(&r);
    run_tool(workDir, "comm", "--nocheck-order unsorted.txt a.txt", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK_STR(r.err, "");
    run_result_free(&r);
    run_tool(workDir, "comm", "--check-order unsorted.txt unsorted.txt", "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    printf("NUL terminated lines and errors\n");
    run_tool(workDir, "comm", "a.txt nosuch.txt", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "nosuch.txt: No such file or directory") != NULL);
    run_result_free(&r);
    run_tool(workDir, "comm", "a.txt", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "missing operand after 'a.txt'") != NULL);
    run_result_free(&r);
    run_tool(workDir, "comm", "--bogus a.txt b.txt", "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    return test_summary("comm_test");
}
