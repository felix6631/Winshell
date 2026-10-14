/*
 * Tests of realpath, run as the program it is. The comparison with GNU realpath (gnu_compare.sh) covers
 * the rules for paths that are not links, by their relative form; these are the absolute (virtual) form and
 * the junctions of Windows, which are what symbolic links are here.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* output, int status) {
    RunResult r;
    run_tool(workDir, "realpath", arguments, "", &r);
    CHECK_STR(r.out, output);
    CHECK_INT(r.exitCode, status);
    run_result_free(&r);
}

int main(void) {
    char arguments[MAX_PATH * 4], expected[MAX_PATH * 4], command[MAX_PATH * 4];
    int made;

    tools_init("realpath_test");

    reset_dir(workDir);
    make_directory("real");
    make_directory("real\\sub");
    write_text(workDir, "real\\file.txt", "x");

    printf("absolute paths\n");
    snprintf(expected, sizeof expected, "%s\n", virtualWorkDir);
    expect(".", expected, 0);
    snprintf(expected, sizeof expected, "%s/real\n", virtualWorkDir);
    expect("real", expected, 0);
    expect("real/", expected, 0);
    expect("./real/sub/..", expected, 0);
    snprintf(expected, sizeof expected, "%s/real/file.txt\n", virtualWorkDir);
    expect("real//file.txt", expected, 0);
    snprintf(arguments, sizeof arguments, "%s/real/../real/file.txt", virtualWorkDir);
    expect(arguments, expected, 0);
    expect("/", "/\n", 0);
    expect("/c", "/c\n", 0);
    expect("/c/./Windows/..", "/c\n", 0);
    expect("/..", "/\n", 0);

    // Windows paths with a drive
    expect("C:\\Windows\\..", "/c\n", 0);

    printf("what has to exist\n");
    snprintf(expected, sizeof expected, "%s/real/missing\n", virtualWorkDir);
    expect("real/missing", expected, 0);              // the last component may be missing
    expect("real/missing/deeper", "", 1);             // the others may not
    expect("-e real/missing", "", 1);
    snprintf(expected, sizeof expected, "%s/real/missing/deeper\n", virtualWorkDir);
    expect("-m real/missing/deeper", expected, 0);
    expect("real/file.txt/x", "", 1);                 // a file is not a directory
    expect("real/file.txt/", "", 1);
    expect("-q real/missing/deeper", "", 1);

    printf("relative paths\n");
    expect("--relative-to=real real/sub", "sub\n", 0);
    expect("--relative-to=real/sub real", "..\n", 0);
    expect("--relative-to=real real", ".\n", 0);
    expect("--relative-to=real/sub .", "../..\n", 0);
    snprintf(expected, sizeof expected, "%s/real\n", virtualWorkDir);
    expect("--relative-base=real/sub real", expected, 0);     // outside the base: absolute
    expect("--relative-base=real real/sub", "sub\n", 0);

    printf("junctions\n");
    snprintf(command, sizeof command, "mklink /J \"%s\\link\" \"%s\\real\" > nul", workDir, workDir);
    made = system(command);
    if (made != 0) {
        printf("(skipped: no junction could be made)\n");
    } else {
        snprintf(expected, sizeof expected, "%s/real\n", virtualWorkDir);
        expect("link", expected, 0);
        snprintf(expected, sizeof expected, "%s/real/file.txt\n", virtualWorkDir);
        expect("link/file.txt", expected, 0);
        snprintf(expected, sizeof expected, "%s/link\n", virtualWorkDir);
        expect("-s link", expected, 0);                  // not followed
        snprintf(expected, sizeof expected, "%s/real\n", virtualWorkDir);
        expect("link/sub/..", expected, 0);
    }

    return test_summary("realpath_test");
}
