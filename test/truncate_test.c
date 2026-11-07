/*
 * Tests of truncate, run as the program it is: absolute and relative sizes, the suffixes, -r, -c, files that are
 * created, and the errors. GNU truncate is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static long long size_of(const char* name) {
    char path[MAX_PATH * 2];
    WIN32_FILE_ATTRIBUTE_DATA data;

    snprintf(path, sizeof path, "%s\\%s", workDir, name);
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data))
        return -1;
    return ((long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}

/** Makes a file of `start` bytes (none when negative), runs truncate with the arguments and checks size and status. */
static void check(long long start, const char* arguments, long long expectedSize, int status) {
    RunResult r;
    char path[MAX_PATH * 2];

    snprintf(path, sizeof path, "%s\\f.bin", workDir);
    DeleteFileA(path);
    if (start >= 0)
        make_sized_file(workDir, "f.bin", (size_t)start, FILE_ATTRIBUTE_NORMAL);
    run_tool(workDir, "truncate", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    if (size_of("f.bin") != expectedSize)
        printf("    for: truncate %s (from %lld)\n    got: %lld\n", arguments, start, size_of("f.bin"));
    CHECK(size_of("f.bin") == expectedSize);
    run_result_free(&r);
}

int main(void) {
    tools_init("truncate_test");
    reset_dir(workDir);
    make_sized_file(workDir, "ref.bin", 40, FILE_ATTRIBUTE_NORMAL);

    printf("absolute\n");
    check(100, "-s 10 f.bin", 10, 0);
    check(100, "--size=0 f.bin", 0, 0);
    check(10, "-s 100 f.bin", 100, 0);
    check(-1, "-s 5 f.bin", 5, 0);                      // created
    check(-1, "-c -s 5 f.bin", -1, 0);                  // -c: not created
    check(100, "-s 1K f.bin", 1024, 0);
    check(100, "-s 1KB f.bin", 1000, 0);
    check(0, "-s 2M f.bin", 2 * 1024 * 1024, 0);

    printf("relative\n");
    check(100, "-s +10 f.bin", 110, 0);
    check(100, "-s -10 f.bin", 90, 0);
    check(5, "-s -10 f.bin", 0, 0);                     // not below 0
    check(100, "-s <50 f.bin", 50, 0);
    check(30, "-s <50 f.bin", 30, 0);
    check(100, "-s >150 f.bin", 150, 0);
    check(200, "-s >150 f.bin", 200, 0);
    check(100, "-s /30 f.bin", 90, 0);
    check(100, "-s %30 f.bin", 120, 0);

    printf("reference\n");
    check(100, "-r ref.bin f.bin", 40, 0);
    check(100, "-r ref.bin -s +5 f.bin", 45, 0);
    check(100, "-r ref.bin -s %7 f.bin", 42, 0);
    check(100, "-r nosuchfile f.bin", 100, 1);

    printf("errors\n");
    check(100, "f.bin", 100, 1);                        // no size
    check(100, "-s x f.bin", 100, 1);
    check(100, "-s 1x f.bin", 100, 1);
    check(100, "-s %0 f.bin", 100, 1);
    check(100, "-s 10", 100, 1);                        // no file
    check(100, "-s 10 nodir\\x f.bin", 10, 1);          // the others are still done
    expect_run("truncate", "--version", "", "truncate (Winshell) 0.1\n", 0);

    return test_summary("truncate_test");
}
