/*
 * Tests of ln, run as the program it is: hard links (the same file under two names), where a link goes (the forms
 * of the command), -f -i -b -v -T -t, symbolic links to directories (junctions) and the errors.
 * GNU ln is compared with by gnu_compare.sh (hard links).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* expected, int status) {
    expect_run("ln", arguments, "", expected, status);
}

int main(void) {
    RunResult r;
    char expected[MAX_PATH * 2];

    tools_init("ln_test");
    reset_dir(workDir);
    write_text(workDir, "a.txt", "hello");
    write_text(workDir, "b.txt", "other");
    make_directory("dir");

    printf("hard links\n");
    expect("a.txt link1", "", 0);
    write_text(workDir, "a.txt", "changed");
    expect_content(workDir, "link1", "changed");                       // the same file
    expect("a.txt dir", "", 0);                                          // into a directory, under its own name
    expect_content(workDir, "dir\\a.txt", "changed");
    expect("a.txt b.txt dir/", "", 1);                                   // a.txt exists there already
    expect("-f a.txt b.txt", "", 0);                                     // b.txt replaced
    expect_content(workDir, "b.txt", "changed");
    expect("-v a.txt v1", "'v1' => 'a.txt'\n", 0);
    expect("-t dir b.txt", "", 1);                                       // exists
    expect("-ft dir b.txt", "", 0);
    write_text(workDir, "c.txt", "c");
    expect("c.txt", "", 1);                                              // a link in the current directory: c.txt exists
    expect("-T c.txt dir", "", 1);                                       // dir is not a file
    expect("-b -f c.txt b.txt", "", 0);
    CHECK(path_exists_in(workDir, "b.txt~"));
    expect_content(workDir, "b.txt~", "changed");
    expect_content(workDir, "b.txt", "c");
    write_text(workDir, "d.txt", "d");
    expect("-bS .old -f d.txt b.txt", "", 0);
    CHECK(path_exists_in(workDir, "b.txt.old"));

    printf("hard link errors\n");
    expect("a.txt a.txt", "", 1);                                        // the same file
    expect("-f a.txt a.txt", "", 1);
    expect_content(workDir, "a.txt", "changed");                          // and it is still there
    expect("nosuchfile z", "", 1);
    expect("dir z", "", 1);                                              // a hard link to a directory
    expect("a.txt nodir/z", "", 1);
    expect("", "", 1);
    expect("-T a.txt b.txt c.txt", "", 1);
    expect("a.txt b.txt c.txt nosuchdir", "", 1);

    printf("symbolic links to directories\n");
    expect("-s dir sl", "", 0);                                          // a junction when Windows has no symbolic links
    run_tool(workDir, "readlink", "sl", "", &r);
    snprintf(expected, sizeof expected, "%s/dir\n", virtualWorkDir);
    CHECK_STR(r.out, expected);
    run_result_free(&r);
    expect("-sv dir sl2", "'sl2' -> 'dir'\n", 0);
    expect("-sT dir sl", "", 1);                                         // sl is a link to a directory, not a file
    expect("-snf dir sl", "", 0);

    printf("errors\n");
    expect("-x", "", 1);
    expect("--version", "ln (Winshell) 0.1\n", 0);

    return test_summary("ln_test");
}
