/*
 * Tests of basename and dirname, run as the programs they are. The comparison with GNU (gnu_compare.sh)
 * covers the rules in bulk; these are what is different on purpose (the backslash is a separator, a name of
 * two slashes is the root as on Linux) and the output with NUL.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void expect(const char* tool, const char* arguments, const char* output, int status) {
    RunResult r;
    run_tool(workDir, tool, arguments, "", &r);
    CHECK_STR(r.out, output);
    CHECK_INT(r.exitCode, status);
    run_result_free(&r);
}

int main(void) {
    tools_init("names_test");

    printf("basename\n");
    expect("basename", "/usr/bin/sort", "sort\n", 0);
    expect("basename", "include/stdio.h .h", "stdio\n", 0);
    expect("basename", ".h .h", ".h\n", 0);                     // the suffix is not the whole name
    expect("basename", "-s .h include/stdio.h", "stdio\n", 0);
    expect("basename", "-a a/x b/y", "x\ny\n", 0);
    expect("basename", "/", "/\n", 0);
    expect("basename", "//", "/\n", 0);
    expect("basename", "a//", "a\n", 0);
    expect("basename", "\"\"", "\n", 0);
    expect("basename", "C:\\Users\\me\\file.txt", "file.txt\n", 0);
    expect("basename", "/c/Users/한글/이름.txt .txt", "이름\n", 0);
    expect("basename", "", "", 1);
    expect("basename", "a b c", "", 1);

    {
        RunResult r;
        run_tool(workDir, "basename", "-z -a a/b c/d", "", &r);
        CHECK_INT((int)r.outLength, 4);
        CHECK(r.outLength == 4 && memcmp(r.out, "b\0d\0", 4) == 0);
        run_result_free(&r);
    }

    printf("dirname\n");
    expect("dirname", "/usr/bin/", "/usr\n", 0);
    expect("dirname", "stdio.h", ".\n", 0);
    expect("dirname", "a/b/c", "a/b\n", 0);
    expect("dirname", "/a", "/\n", 0);
    expect("dirname", "//", "/\n", 0);
    expect("dirname", "a//b", "a\n", 0);
    expect("dirname", "\"\"", ".\n", 0);
    expect("dirname", "dir1/str dir2/str", "dir1\ndir2\n", 0);
    expect("dirname", "C:\\Users\\me\\file.txt", "C:\\Users\\me\n", 0);
    expect("dirname", "/c/Users/한글/이름.txt", "/c/Users/한글\n", 0);
    expect("dirname", "", "", 1);

    {
        RunResult r;
        run_tool(workDir, "dirname", "-z a/b c/d", "", &r);
        CHECK(r.outLength == 4 && memcmp(r.out, "a\0c\0", 4) == 0);
        run_result_free(&r);
    }

    return test_summary("names_test");
}
