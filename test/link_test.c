/*
 * Tests of link, run as the program it is: a hard link to a file, and what it refuses.
 * GNU link is compared with by gnu_compare.sh (section link, which has unlink too).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* expected, int status) {
    expect_run("link", arguments, "", expected, status);
}

int main(void) {
    tools_init("link_test");
    reset_dir(workDir);
    write_text(workDir, "a.txt", "hello");
    make_directory("dir");

    printf("links\n");
    expect("a.txt b.txt", "", 0);
    expect_content(workDir, "b.txt", "hello");
    write_text(workDir, "a.txt", "changed");
    expect_content(workDir, "b.txt", "changed");                // the same file under two names
    expect("a.txt dir/c.txt", "", 0);
    expect_content(workDir, "dir\\c.txt", "changed");

    printf("what is refused\n");
    expect("a.txt b.txt", "", 1);                                 // exists
    expect("nosuchfile z.txt", "", 1);
    expect("dir z.txt", "", 1);                                   // a directory
    expect("a.txt nodir/z.txt", "", 1);
    expect("a.txt", "", 1);
    expect("", "", 1);
    expect("a.txt b.txt c.txt", "", 1);
    expect("--version", "link (Winshell) 0.1\n", 0);

    return test_summary("link_test");
}
