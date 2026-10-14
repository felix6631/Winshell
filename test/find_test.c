/*
 * Tests of find, run as the program it is. The comparison with GNU find (gnu_compare.sh) covers
 * the tests and operators on a tree; these are the actions that change things (-delete, -exec),
 * the paths of the virtual file system, UTF-8 names and the exit statuses.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* output, int status) {
    expect_run("find", arguments, "", output, status);
}

static void make_tree(void) {
    reset_dir(workDir);
    make_directory("t");
    make_directory("t\\a");
    make_directory("t\\a\\b");
    make_directory("t\\한글");
    write_text(workDir, "t\\top.txt", "top\n");
    write_text(workDir, "t\\a\\one.c", "1\n");
    write_text(workDir, "t\\a\\b\\two.c", "22\n");
    write_text(workDir, "t\\한글\\파일.txt", "x");
    write_text(workDir, "t\\empty.txt", "");
}

int main(void) {
    RunResult r;
    char arguments[MAX_PATH * 3], expected[MAX_PATH * 4];

    tools_init("find_test");
    make_tree();

    printf("walk\n");
    expect("t", "t\nt/a\nt/a/b\nt/a/b/two.c\nt/a/one.c\nt/empty.txt\nt/top.txt\nt/한글\nt/한글/파일.txt\n", 0);
    expect("t -type d", "t\nt/a\nt/a/b\nt/한글\n", 0);
    expect("t/ -maxdepth 1", "t/\nt/a\nt/empty.txt\nt/top.txt\nt/한글\n", 0);
    expect("t -mindepth 1 -maxdepth 1 -type f", "t/empty.txt\nt/top.txt\n", 0);
    expect("t -depth -type d", "t/a/b\nt/a\nt/한글\nt\n", 0);
    expect("t -name \"*.c\"", "t/a/b/two.c\nt/a/one.c\n", 0);
    expect("t -name a -prune -o -type f -print", "t/empty.txt\nt/top.txt\nt/한글/파일.txt\n", 0);
    expect("t -name \"파일*\"", "t/한글/파일.txt\n", 0);
    expect("t -name \"?.txt\"", "", 0);
    expect("t -name \"???.txt\"", "t/top.txt\n", 0);
    expect("t -empty", "t/empty.txt\n", 0);
    expect("t -size 0 -type f", "t/empty.txt\n", 0);
    expect("t -type f -size -3c", "t/a/one.c\nt/empty.txt\nt/한글/파일.txt\n", 0);
    expect("t -type f -printf \"%P:%s\\n\"", "a/b/two.c:3\na/one.c:2\nempty.txt:0\ntop.txt:4\n한글/파일.txt:1\n", 0);
    expect("t -maxdepth 0 -printf \"[%P][%f][%h]\\n\"", "[][t][.]\n", 0);
    expect("t/a -type f -a ( -name one.c -o -name two.c )", "t/a/b/two.c\nt/a/one.c\n", 0);

    // the current directory, and the default path
    run_tool(workDir, "find", "", "", &r);
    CHECK(strstr(r.out, "./t/top.txt\n") != NULL);
    CHECK(strncmp(r.out, ".\n", 2) == 0);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    // the virtual file system
    snprintf(arguments, sizeof arguments, "%s/t/a -type f", virtualWorkDir);
    snprintf(expected, sizeof expected, "%s/t/a/b/two.c\n%s/t/a/one.c\n", virtualWorkDir, virtualWorkDir);
    expect(arguments, expected, 0);

    printf("actions\n");
    expect("t -type f -name \"*.c\" -exec echo [{}] ;", "[t/a/b/two.c]\n[t/a/one.c]\n", 0);
    expect("t -type f -name \"*.c\" -exec echo files {} +", "files t/a/b/two.c t/a/one.c\n", 0);
    expect("t -name top.txt -exec echo a{}b ;", "at/top.txtb\n", 0);
    expect("t -name top.txt -exec false ; -print", "", 0);
    expect("t -name top.txt -exec true ; -print", "t/top.txt\n", 0);
    expect("t -name \"*.c\" -execdir echo {} ;", "./two.c\n./one.c\n", 0);
    expect("t -name nosuch -exec echo no {} +", "", 0);
    expect("t -name top.txt -exec nosuchprogram_xyz {} ;", "", 1);

    run_tool(workDir, "find", "t -name top.txt -ok echo {} ;", "n\n", &r);
    CHECK_STR(r.out, "");
    run_result_free(&r);
    run_tool(workDir, "find", "t -name top.txt -ok echo {} ;", "y\n", &r);
    CHECK_STR(r.out, "t/top.txt\n");
    run_result_free(&r);

    expect("t -name top.txt -print0", "t/top.txt\0", 0);

    // -delete: files, then the empty directories under them (it implies -depth)
    expect("t/a -delete", "", 0);
    CHECK(!path_exists_in(workDir, "t\\a"));
    CHECK(path_exists_in(workDir, "t\\top.txt"));
    expect("t -name \"*.txt\" -type f -delete", "", 0);
    CHECK(!path_exists_in(workDir, "t\\top.txt"));
    CHECK(!path_exists_in(workDir, "t\\한글\\파일.txt"));
    CHECK(path_exists_in(workDir, "t\\한글"));

    // a read-only file is deleted as well
    make_tree();
    {
        char path[MAX_PATH * 2];
        snprintf(path, sizeof path, "%s\\t\\top.txt", workDir);
        SetFileAttributesA(path, FILE_ATTRIBUTE_READONLY);
        expect("t -name top.txt -delete", "", 0);
        CHECK(!path_exists_in(workDir, "t\\top.txt"));
    }

    printf("errors\n");
    make_tree();
    run_tool(workDir, "find", "nosuch t/a -maxdepth 0", "", &r);
    CHECK_STR(r.out, "t/a\n");
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "nosuch") != NULL);
    run_result_free(&r);
    expect("t -bogus", "", 1);
    expect("t -name", "", 1);
    expect("t ( -name x", "", 1);
    expect("t -maxdepth x", "", 1);
    expect("t -type x", "", 1);

    return test_summary("find_test");
}
