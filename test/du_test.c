/*
 * Tests of du, run as the program it is, on a tree of files of known sizes: the order and the sums, -a -s -c -d,
 * -b and the block sizes, -h, hard links counted once, --exclude and the errors.
 * GNU du is compared with by gnu_compare.sh (sizes in bytes).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* expected, int status) {
    expect_run("du", arguments, "", expected, status);
}

int main(void) {
    tools_init("du_test");
    reset_dir(workDir);
    make_directory("t");
    make_directory("t\\a");
    make_directory("t\\a\\b");
    make_directory("t\\c");
    make_sized_file(workDir, "t\\f1", 5000, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(workDir, "t\\a\\f2", 100, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(workDir, "t\\a\\b\\f3", 10000, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(workDir, "t\\c\\zero", 0, FILE_ATTRIBUTE_NORMAL);

    printf("sums\n");
    expect("-b t", "10000\tt/a/b\n10100\tt/a\n0\tt/c\n15100\tt\n", 0);
    expect("-bs t", "15100\tt\n", 0);
    expect("-b -d 1 t", "10100\tt/a\n0\tt/c\n15100\tt\n", 0);
    expect("-b -d 0 t", "15100\tt\n", 0);
    expect("-ab t", "10000\tt/a/b/f3\n10000\tt/a/b\n100\tt/a/f2\n10100\tt/a\n0\tt/c/zero\n0\tt/c\n5000\tt/f1\n15100\tt\n", 0);
    expect("-bc t/f1 t/a", "5000\tt/f1\n10000\tt/a/b\n10100\tt/a\n15100\ttotal\n", 0);
    expect("-b t/f1", "5000\tt/f1\n", 0);
    expect("-bS t", "10000\tt/a/b\n100\tt/a\n0\tt/c\n5000\tt\n", 0);          // the directories without what is below
    expect("-b t t/a", "10000\tt/a/b\n10100\tt/a\n0\tt/c\n15100\tt\n", 0);    // t/a is part of t: not counted again
    expect("-b --exclude=f* -a t", "0\tt/a/b\n0\tt/a\n0\tt/c/zero\n0\tt/c\n0\tt\n", 0);

    printf("units\n");
    expect("--apparent-size -k t/f1", "5\tt/f1\n", 0);                      // rounded up
    expect("-b -B 1000 t/f1", "5\tt/f1\n", 0);
    expect("-b -B K t/f1", "5K\tt/f1\n", 0);                                 // a unit without a number is written
    expect("-b -B KB t/f1", "5kB\tt/f1\n", 0);
    expect("--apparent-size -m t/f1", "1\tt/f1\n", 0);
    expect("-bh t/f1", "4.9K\tt/f1\n", 0);
    expect("-b --si t/f1", "5.0k\tt/f1\n", 0);
    expect("--apparent-size --si t/f1", "5.0k\tt/f1\n", 0);
    expect("-bh t/a/b", "9.8K\tt/a/b\n", 0);
    expect("-bh t/a/f2", "100\tt/a/f2\n", 0);
    expect("-0 -b t/f1", "5000\tt/f1\0", 0);

    printf("hard links\n");
    {
        char first[MAX_PATH * 2], second[MAX_PATH * 2];

        make_directory("h");
        make_directory("h\\sub");
        make_sized_file(workDir, "h\\one", 3000, FILE_ATTRIBUTE_NORMAL);
        snprintf(first, sizeof first, "%s\\h\\one", workDir);
        snprintf(second, sizeof second, "%s\\h\\sub\\two", workDir);
        if (CreateHardLinkA(second, first, NULL)) {
            expect("-b h", "0\th/sub\n3000\th\n", 0);                        // one comes first: sub/two is the same file
            expect("-bl h", "3000\th/sub\n6000\th\n", 0);                    // -l counts every name
        }
    }

    printf("errors\n");
    expect("-b nosuchfile", "", 1);
    expect("-b t/f1 nosuchfile", "5000\tt/f1\n", 1);
    expect("-d x t", "", 1);
    expect("-B 0 t", "", 1);
    expect("-B x t", "", 1);
    expect("--bogus", "", 1);
    expect("--version", "du (Winshell) 0.1\n", 0);

    return test_summary("du_test");
}
