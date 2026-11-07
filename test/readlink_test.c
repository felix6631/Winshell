/*
 * Tests of readlink, run as the program it is, on junctions (what symbolic links are here): the target, files that
 * are not links, and the canonical forms -f -e -m. GNU readlink is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* expected, int status) {
    expect_run("readlink", arguments, "", expected, status);
}

int main(void) {
    char command[MAX_PATH * 3], expected[MAX_PATH * 2];

    tools_init("readlink_test");
    reset_dir(workDir);
    make_directory("real");
    make_directory("real\\sub");
    write_text(workDir, "real\\file.txt", "x");

    printf("not links\n");
    expect("real", "", 1);
    expect("real/file.txt", "", 1);
    expect("nosuchfile", "", 1);
    expect("-n real", "", 1);

    printf("junctions\n");
    snprintf(command, sizeof command, "mklink /J \"%s\\link\" \"%s\\real\" > nul", workDir, workDir);
    if (system(command) != 0) {
        printf("(skipped: no junction could be made)\n");
    } else {
        snprintf(expected, sizeof expected, "%s/real\n", virtualWorkDir);
        expect("link", expected, 0);
        snprintf(expected, sizeof expected, "%s/real", virtualWorkDir);
        expect("-n link", expected, 0);
        snprintf(expected, sizeof expected, "%s/real\n%s/real\n", virtualWorkDir, virtualWorkDir);
        expect("link link", expected, 0);

        printf("canonical\n");
        snprintf(expected, sizeof expected, "%s/real/file.txt\n", virtualWorkDir);
        expect("-f link/file.txt", expected, 0);
        expect("-e link/file.txt", expected, 0);
        snprintf(expected, sizeof expected, "%s/real/missing\n", virtualWorkDir);
        expect("-f link/missing", expected, 0);             // the last component may be missing
        expect("-e link/missing", "", 1);
        snprintf(expected, sizeof expected, "%s/real/a/b\n", virtualWorkDir);
        expect("-m link/a/b", expected, 0);
        expect("-f link/a/b", "", 1);
        snprintf(expected, sizeof expected, "%s/real", virtualWorkDir);
        expect("-fn link", expected, 0);
    }
    snprintf(expected, sizeof expected, "%s/real\n", virtualWorkDir);
    expect("-f real/sub/..", expected, 0);

    printf("errors\n");
    expect("", "", 1);
    expect("-x", "", 1);
    expect("--version", "readlink (Winshell) 0.1\n", 0);

    return test_summary("readlink_test");
}
