/*
 * Tests of split, run as the program it is: the pieces by lines, bytes and line bytes, -n, the names of the pieces
 * (-d, -x, -a, suffixes that grow) and the errors. GNU split is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void run_split(const char* arguments, const char* input) {
    RunResult r;

    run_tool(workDir, "split", arguments, input, &r);
    run_result_free(&r);
}

static void prepare(void) {
    reset_dir(workDir);
}

static void expect_piece(const char* name, const char* expected) {
    if (!path_exists_in(workDir, name)) {
        g_checks++;
        g_failures++;
        printf("  FAIL missing piece %s\n", name);
        return;
    }
    expect_content(workDir, name, expected);
}

int main(void) {
    tools_init("split_test");

    printf("lines\n");
    prepare();
    run_split("-l 2", "1\n2\n3\n4\n5\n");
    expect_piece("xaa", "1\n2\n");
    expect_piece("xab", "3\n4\n");
    expect_piece("xac", "5\n");
    CHECK(!path_exists_in(workDir, "xad"));
    prepare();
    run_split("-2", "a\nb\nc\n");                                    // -NUM is -l NUM
    expect_piece("xaa", "a\nb\n");
    expect_piece("xab", "c\n");
    prepare();
    run_split("-l 2", "a\nb\nc");                                     // the last line has no line feed
    expect_piece("xab", "c");
    prepare();
    run_split("-l 5", "");                                            // nothing in, nothing out
    CHECK(!path_exists_in(workDir, "xaa"));
    prepare();
    run_split("-t : -l 2", "a:b:c:d:e");
    expect_piece("xaa", "a:b:");
    expect_piece("xac", "e");

    printf("bytes\n");
    prepare();
    run_split("-b 4", "0123456789");
    expect_piece("xaa", "0123");
    expect_piece("xab", "4567");
    expect_piece("xac", "89");
    prepare();
    run_split("-b 1K", "0123456789");
    expect_piece("xaa", "0123456789");
    prepare();
    run_split("-C 6", "ab\ncd\nef\ngh\n");                             // lines that fit
    expect_piece("xaa", "ab\ncd\n");
    expect_piece("xab", "ef\ngh\n");
    prepare();
    run_split("-C 4", "abcdefgh\nij\n");                              // a line that is too long is cut
    expect_piece("xaa", "abcd");
    expect_piece("xab", "efgh");
    expect_piece("xac", "\nij\n");                                     // the next line fits after the line feed

    printf("pieces\n");
    prepare();
    run_split("-n 3", "0123456789");                                   // 3 files: the last has the rest
    expect_piece("xaa", "012");
    expect_piece("xab", "345");
    expect_piece("xac", "6789");
    prepare();
    run_split("-n l/2", "aa\nbb\ncc\ndd\n");
    expect_piece("xaa", "aa\nbb\n");
    expect_piece("xab", "cc\ndd\n");
    prepare();
    run_split("-n r/2", "1\n2\n3\n4\n5\n");                           // round robin
    expect_piece("xaa", "1\n3\n5\n");
    expect_piece("xab", "2\n4\n");
    prepare();
    {
        RunResult r;

        run_tool(workDir, "split", "-n 2/3", "0123456789", &r);       // piece 2 of 3 to standard output
        CHECK_STR(r.out, "345");
        run_result_free(&r);
    }

    printf("names\n");
    prepare();
    run_split("-d -l 1", "a\nb\nc\n");
    expect_piece("x00", "a\n");
    expect_piece("x02", "c\n");
    prepare();
    run_split("-x -l 1", "a\nb\n");
    expect_piece("x00", "a\n");
    prepare();
    run_split("-a 3 -l 1", "a\nb\n");
    expect_piece("xaaa", "a\n");
    expect_piece("xaab", "b\n");
    prepare();
    run_split("-l 1 --additional-suffix=.txt - out_", "a\nb\n");
    expect_piece("out_aa.txt", "a\n");
    expect_piece("out_ab.txt", "b\n");
    prepare();
    run_split("-l 1", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n");
    expect_piece("xba", "27\n");                                       // aa .. az, ba
    {
        char text[2048];
        int i, used = 0;

        prepare();
        for (i = 1; i <= 100; i++)
            used += snprintf(text + used, sizeof text - used, "%d\n", i);
        run_split("-d -l 1", text);                                     // x00 .. x89, then x9000 ...
        expect_piece("x89", "90\n");
        expect_piece("x9000", "91\n");
        expect_piece("x9009", "100\n");
        CHECK(!path_exists_in(workDir, "x90"));
        prepare();
        run_split("-a 2 -d -l 1", text);                                // with -a the suffixes do not grow
        expect_piece("x99", "100\n");
        CHECK(!path_exists_in(workDir, "x9000"));
    }

    printf("errors\n");
    expect_run("split", "-l 0", "a\n", "", 1);
    expect_run("split", "-b 0", "a\n", "", 1);
    expect_run("split", "-b 5x", "a\n", "", 1);
    expect_run("split", "-n 0", "a\n", "", 1);
    expect_run("split", "-n 4/3", "a\n", "", 1);
    expect_run("split", "-a x", "a\n", "", 1);
    expect_run("split", "-l 1 nosuchfile", "", "", 1);
    expect_run("split", "-l 1 --additional-suffix=a/b", "a\n", "", 1);
    expect_run("split", "-x", "", "", 0);
    expect_run("split", "--bogus", "", "", 1);
    expect_run("split", "--version", "", "split (Winshell) 0.1\n", 0);

    return test_summary("split_test");
}
