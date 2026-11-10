/*
 * Tests of dir, run as the program it is: it lists like "ls -C -b" (in columns also when the output is not a console, with
 * blanks and backslashes written as escapes), and of the -b option of ls.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* expected) {
    expect_run("dir", arguments, "", expected, 0);
}

int main(void) {
    RunResult r;

    tools_init("dir_test");
    reset_dir(workDir);
    write_text(workDir, "a b", "x");
    write_text(workDir, "c.txt", "x");
    write_text(workDir, "d.txt", "x");
    make_directory("sub");
    write_text(workDir, "sub\\inner.txt", "y");

    printf("dir\n");
    expect("", "a\\ b  c.txt  d.txt  sub\n");                         // columns, and the blank is escaped
    expect("-1", "a\\ b\nc.txt\nd.txt\nsub\n");
    expect("-a", ".  ..  a\\ b  c.txt  d.txt  sub\n");
    expect("-F", "a\\ b  c.txt  d.txt  sub/\n");
    expect("-r", "sub  d.txt  c.txt  a\\ b\n");
    expect("sub", "inner.txt\n");
    expect("c.txt sub", "c.txt\n\nsub:\ninner.txt\n");
    expect("-d sub", "sub\n");
    expect_run("dir", "--version", "", "dir (Winshell) 0.1\n", 0);

    run_tool(workDir, "dir", "-l", "", &r);                          // the long format is still possible
    CHECK_INT(r.exitCode, 0);
    CHECK(strstr(r.out, "total ") == r.out);
    CHECK(strstr(r.out, " a\\ b\n") != NULL);
    run_result_free(&r);
    run_tool(workDir, "dir", "nosuchfile_zz", "", &r);
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "dir: cannot access 'nosuchfile_zz'") != NULL);
    run_result_free(&r);

    printf("ls -b\n");
    expect_run("ls", "-b", "", "a\\ b\nc.txt\nd.txt\nsub\n", 0);
    expect_run("ls", "", "", "a b\nc.txt\nd.txt\nsub\n", 0);          // without -b the names are as they are
    expect_run("ls", "--escape", "", "a\\ b\nc.txt\nd.txt\nsub\n", 0);
    return test_summary("dir_test");
}
