/*
 * Tests of unlink, run as the program it is: it removes one file, also a read-only one, and not a directory.
 * GNU unlink is compared with by gnu_compare.sh (section link).
 */

#include "testtools.h"

static void expect(const char* arguments, int status) {
    expect_run("unlink", arguments, "", "", status);
}

int main(void) {
    tools_init("unlink_test");
    reset_dir(workDir);
    write_text(workDir, "a.txt", "hello");
    write_text(workDir, "ro.txt", "x");
    SetFileAttributesA("ro.txt", FILE_ATTRIBUTE_NORMAL);
    {
        char path[MAX_PATH * 2];

        snprintf(path, sizeof path, "%s\\ro.txt", workDir);
        SetFileAttributesA(path, FILE_ATTRIBUTE_READONLY);
    }
    make_directory("dir");
    write_text(workDir, "dir\\inside.txt", "y");

    printf("files\n");
    expect("a.txt", 0);
    CHECK(!path_exists_in(workDir, "a.txt"));
    expect("ro.txt", 0);                                          // the permissions of the file do not matter
    CHECK(!path_exists_in(workDir, "ro.txt"));

    printf("what is refused\n");
    expect("a.txt", 1);                                           // gone
    expect("dir", 1);                                             // a directory
    CHECK(path_exists_in(workDir, "dir\\inside.txt"));
    expect("", 1);
    expect("dir/inside.txt dir/inside.txt", 1);
    CHECK(path_exists_in(workDir, "dir\\inside.txt"));
    expect_run("unlink", "--version", "", "unlink (Winshell) 0.1\n", 0);

    return test_summary("unlink_test");
}
