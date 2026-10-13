/*
 * Tests of rm, cp and mv: files and trees, read-only files, the protection of roots.
 */

#include "testtools.h"

static void test_rm(void) {
    RunResult r;
    char d[MAX_PATH * 2];

    printf("rm\n");
    snprintf(d, sizeof d, "%s\\rm", workDir);
    reset_dir(d);
    write_text(d, "f1.txt", "x");
    write_text(d, "f2.txt", "x");

    run_tool(d, "rm", "f1.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "f1.txt"));
    run_result_free(&r);

    run_tool(d, "rm", "f1.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rm: cannot remove 'f1.txt': No such file or directory") != NULL);
    run_result_free(&r);

    run_tool(d, "rm", "-f f1.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK_STR(r.err, "");
    run_result_free(&r);

    run_tool(d, "rm", "-v f2.txt", NULL, &r);
    CHECK_STR(lf_only(r.out), "removed 'f2.txt'\n");
    CHECK(!path_exists_in(d, "f2.txt"));
    run_result_free(&r);

    // directories need -r (or -d, when they are empty)
    make_dir_in(d, "sub");
    write_text(d, "sub\\x.txt", "x");
    run_tool(d, "rm", "sub", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rm: cannot remove 'sub': Is a directory") != NULL);
    CHECK(path_exists_in(d, "sub\\x.txt"));
    run_result_free(&r);

    run_tool(d, "rm", "-r sub", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "sub"));
    run_result_free(&r);

    make_dir_in(d, "empty");
    run_tool(d, "rm", "-d empty", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "empty"));
    run_result_free(&r);

    make_dir_in(d, "full");
    write_text(d, "full\\y.txt", "y");
    run_tool(d, "rm", "-d full", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "Directory not empty") != NULL);
    run_result_free(&r);

    // a read-only file goes like any other file
    make_sized_file(d, "ro.txt", 1, FILE_ATTRIBUTE_READONLY);
    run_tool(d, "rm", "ro.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "ro.txt"));
    run_result_free(&r);

    // a tree with several levels, read-only files inside, verbose
    make_dir_in(d, "deep");
    make_dir_in(d, "deep\\a");
    make_dir_in(d, "deep\\a\\b");
    write_text(d, "deep\\a\\b\\f.txt", "1");
    make_sized_file(d, "deep\\a\\ro.txt", 1, FILE_ATTRIBUTE_READONLY);
    run_tool(d, "rm", "-rv deep", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "deep"));
    CHECK(strstr(r.out, "removed directory 'deep'") != NULL);
    CHECK(strstr(r.out, "removed 'deep/a/ro.txt'") != NULL);
    run_result_free(&r);

    // -i asks, and the answer decides
    write_text(d, "q.txt", "q");
    run_tool(d, "rm", "-i q.txt", "n\n", &r);
    CHECK(path_exists_in(d, "q.txt"));
    CHECK(strstr(r.err, "rm: remove regular file 'q.txt'?") != NULL);
    run_result_free(&r);
    run_tool(d, "rm", "-i q.txt", "y\n", &r);
    CHECK(!path_exists_in(d, "q.txt"));
    run_result_free(&r);

    // the last of -i and -f wins
    write_text(d, "w.txt", "w");
    run_tool(d, "rm", "-i -f w.txt", "n\n", &r);
    CHECK(!path_exists_in(d, "w.txt"));
    run_result_free(&r);

    // things that are never removed
    run_tool(d, "rm", ".", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "refusing to remove '.' or '..'") != NULL);
    run_result_free(&r);

    // The roots of file systems, in every spelling. These run with -i and the answer "n":
    // should the protection ever fail, nothing is removed, and the test notices the question.
    {
        char drive = virtualWorkDir[1];
        char upper = (char)((drive >= 'a' && drive <= 'z') ? drive - 32 : drive);
        char arguments[64];
        size_t k;
        const char* formats[] = {
            "-ri /",                        // the virtual root
            "-ri /%c",                      // a drive
            "-ri /%c/",
            "-ri %C:\\",                    // Windows notation
            "-ri %C:/",
            "-ri %C:",
            "-ri \\",                       // the root of the current drive
            "-ri \\\\?\\%C:\\",             // the long path prefix
            "-ri --no-preserve-root /%c",   // is not lifted by the option
            "-rfi /%c",
            "-di /%c",
        };

        for (k = 0; k < sizeof formats / sizeof formats[0]; k++) {
            // %c is the lower case drive, %C the upper case one
            char* out = arguments;
            const char* f = formats[k];
            for (; *f != '\0' && out < arguments + sizeof arguments - 2; f++) {
                if (f[0] == '%' && f[1] == 'c') {
                    *out++ = drive;
                    f++;
                } else if (f[0] == '%' && f[1] == 'C') {
                    *out++ = upper;
                    f++;
                } else {
                    *out++ = *f;
                }
            }
            *out = '\0';

            run_tool(d, "rm", arguments, "n\nn\nn\nn\n", &r);
            if (r.exitCode != 1 || strstr(r.err, "dangerous to operate recursively") == NULL ||
                strstr(r.err, "descend into") != NULL || strstr(r.err, "remove directory") != NULL)
                printf("    for: rm %s\n    err: %s\n", arguments, r.err);
            CHECK_INT(r.exitCode, 1);
            CHECK(strstr(r.err, "dangerous to operate recursively") != NULL);
            CHECK(strstr(r.err, "descend into") == NULL);
            run_result_free(&r);
        }
    }

    // a root reached by ".." is refused as ".." before it is even looked at
    {
        char arguments[64];
        snprintf(arguments, sizeof arguments, "-ri /%c/Users/..", virtualWorkDir[1]);
        run_tool(d, "rm", arguments, "n\nn\n", &r);
        CHECK_INT(r.exitCode, 1);
        CHECK(strstr(r.err, "refusing to remove '.' or '..'") != NULL);
        CHECK(strstr(r.err, "descend into") == NULL);
        run_result_free(&r);
    }

    // without -r a root is just a directory
    run_tool(d, "rm", "/", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rm: cannot remove '/': Is a directory") != NULL);
    run_result_free(&r);

    run_tool(d, "rm", "", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rm: missing operand") != NULL);
    run_result_free(&r);

    run_tool(d, "rm", "-f", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_tool(d, "rm", "-rf nosuch", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_tool(d, "rm", "-x", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    run_tool(d, "rm", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: rm") != NULL);
    run_result_free(&r);
}

static void test_cp(void) {
    RunResult r;
    char d[MAX_PATH * 2];

    printf("cp\n");
    snprintf(d, sizeof d, "%s\\cp", workDir);
    reset_dir(d);
    write_text(d, "src.txt", "hello");

    run_tool(d, "cp", "src.txt dst.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "dst.txt", "hello");
    expect_content(d, "src.txt", "hello");
    run_result_free(&r);

    run_tool(d, "cp", "-v src.txt dst2.txt", NULL, &r);
    CHECK_STR(lf_only(r.out), "'src.txt' -> 'dst2.txt'\n");
    run_result_free(&r);

    // all byte values, and a file larger than the copy buffer
    {
        unsigned char* big = malloc(200000);
        size_t i;
        for (i = 0; i < 200000; i++)
            big[i] = (unsigned char)(i * 7 + i / 256);
        write_file(d, "big.bin", big, 200000);
        run_tool(d, "cp", "big.bin big2.bin", NULL, &r);
        CHECK_INT(r.exitCode, 0);
        {
            size_t length = 0;
            char* copy = read_file(d, "big2.bin", &length);
            CHECK_INT(length, 200000);
            CHECK(copy != NULL && memcmp(copy, big, 200000) == 0);
            free(copy);
        }
        run_result_free(&r);
        free(big);
    }

    // an existing file is replaced; -n and -i
    write_text(d, "dst.txt", "old");
    run_tool(d, "cp", "src.txt dst.txt", NULL, &r);
    expect_content(d, "dst.txt", "hello");
    run_result_free(&r);

    write_text(d, "keep.txt", "keep");
    run_tool(d, "cp", "-n src.txt keep.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "keep.txt", "keep");
    run_result_free(&r);

    run_tool(d, "cp", "-i src.txt keep.txt", "n\n", &r);
    expect_content(d, "keep.txt", "keep");
    CHECK(strstr(r.err, "cp: overwrite 'keep.txt'?") != NULL);
    run_result_free(&r);

    run_tool(d, "cp", "-i src.txt keep.txt", "y\n", &r);
    expect_content(d, "keep.txt", "hello");
    run_result_free(&r);

    // errors
    run_tool(d, "cp", "src.txt src.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "'src.txt' and 'src.txt' are the same file") != NULL);
    run_result_free(&r);

    run_tool(d, "cp", "nosuch.txt x.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cp: cannot stat 'nosuch.txt': No such file or directory") != NULL);
    run_result_free(&r);

    run_tool(d, "cp", "src.txt nodir\\x.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot create regular file") != NULL);
    run_result_free(&r);

    run_tool(d, "cp", "", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cp: missing file operand") != NULL);
    run_result_free(&r);

    run_tool(d, "cp", "src.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "missing destination file operand after 'src.txt'") != NULL);
    run_result_free(&r);

    // directories
    make_dir_in(d, "tree");
    make_dir_in(d, "tree\\sub");
    write_text(d, "tree\\a.txt", "A");
    write_text(d, "tree\\sub\\b.txt", "B");

    run_tool(d, "cp", "tree out", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cp: -r not specified; omitting directory 'tree'") != NULL);
    CHECK(!path_exists_in(d, "out"));
    run_result_free(&r);

    run_tool(d, "cp", "-r tree out", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "out\\a.txt", "A");
    expect_content(d, "out\\sub\\b.txt", "B");
    run_result_free(&r);

    // onto an existing directory: into it
    run_tool(d, "cp", "-r tree out", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "out\\tree\\sub\\b.txt", "B");
    run_result_free(&r);

    run_tool(d, "cp", "-r tree tree\\sub", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot copy a directory, 'tree', into itself") != NULL);
    run_result_free(&r);

    run_tool(d, "cp", "-r tree src.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot overwrite non-directory 'src.txt' with directory 'tree'") != NULL);
    run_result_free(&r);

    // several sources, and the target directory option
    make_dir_in(d, "into");
    write_text(d, "s2.txt", "two");
    run_tool(d, "cp", "src.txt s2.txt into", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "into\\src.txt", "hello");
    expect_content(d, "into\\s2.txt", "two");
    run_result_free(&r);

    make_dir_in(d, "into2");
    run_tool(d, "cp", "-t into2 src.txt s2.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "into2\\s2.txt", "two");
    run_result_free(&r);

    run_tool(d, "cp", "src.txt s2.txt dst.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cp: target 'dst.txt' is not a directory") != NULL);
    run_result_free(&r);

    // read-only: the destination protects itself, -f overrides; the attribute is copied
    make_sized_file(d, "ro.txt", 3, FILE_ATTRIBUTE_READONLY);
    run_tool(d, "cp", "src.txt ro.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "Permission denied") != NULL);
    run_result_free(&r);
    run_tool(d, "cp", "-f src.txt ro.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "ro.txt", "hello");
    run_result_free(&r);

    make_sized_file(d, "rosrc.txt", 2, FILE_ATTRIBUTE_READONLY);
    run_tool(d, "cp", "rosrc.txt rocopy.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK((attributes_in(d, "rocopy.txt") & FILE_ATTRIBUTE_READONLY) != 0);
    run_result_free(&r);

    // times: a new file is new, -p keeps the time of the source
    set_mtime(d, "src.txt", 2001, 6, 15);
    run_tool(d, "cp", "src.txt fresh.txt", NULL, &r);
    CHECK(year_of(d, "fresh.txt") > 2001);
    run_result_free(&r);
    run_tool(d, "cp", "-p src.txt kept.txt", NULL, &r);
    CHECK_INT(year_of(d, "kept.txt"), 2001);
    run_result_free(&r);

    run_tool(d, "cp", "-a tree archive", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "archive\\sub\\b.txt", "B");
    run_result_free(&r);

    run_tool(d, "cp", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: cp") != NULL);
    run_result_free(&r);
}

static void test_mv(void) {
    RunResult r;
    char d[MAX_PATH * 2];

    printf("mv\n");
    snprintf(d, sizeof d, "%s\\mv", workDir);
    reset_dir(d);
    write_text(d, "a.txt", "A");

    run_tool(d, "mv", "a.txt b.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "a.txt"));
    expect_content(d, "b.txt", "A");
    run_result_free(&r);

    run_tool(d, "mv", "-v b.txt c.txt", NULL, &r);
    CHECK_STR(lf_only(r.out), "renamed 'b.txt' -> 'c.txt'\n");
    run_result_free(&r);

    // into a directory
    make_dir_in(d, "dir");
    run_tool(d, "mv", "c.txt dir", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "dir\\c.txt", "A");
    CHECK(!path_exists_in(d, "c.txt"));
    run_result_free(&r);

    // replacing, -n, -i
    write_text(d, "x.txt", "X");
    write_text(d, "y.txt", "Y");
    run_tool(d, "mv", "x.txt y.txt", NULL, &r);
    expect_content(d, "y.txt", "X");
    CHECK(!path_exists_in(d, "x.txt"));
    run_result_free(&r);

    write_text(d, "p.txt", "P");
    write_text(d, "q.txt", "Q");
    run_tool(d, "mv", "-n p.txt q.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "q.txt", "Q");
    CHECK(path_exists_in(d, "p.txt"));
    run_result_free(&r);

    run_tool(d, "mv", "-i p.txt q.txt", "n\n", &r);
    expect_content(d, "q.txt", "Q");
    CHECK(strstr(r.err, "mv: overwrite 'q.txt'?") != NULL);
    run_result_free(&r);
    run_tool(d, "mv", "-i p.txt q.txt", "y\n", &r);
    expect_content(d, "q.txt", "P");
    run_result_free(&r);

    // a read-only destination is replaced, as unlinking does not depend on it
    make_sized_file(d, "ro.txt", 1, FILE_ATTRIBUTE_READONLY);
    write_text(d, "n.txt", "N");
    run_tool(d, "mv", "n.txt ro.txt", NULL, &r);
    if (r.exitCode != 0)
        printf("    mv said: %s\n", r.err != NULL ? r.err : "(nothing)");
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "ro.txt", "N");
    run_result_free(&r);

    // directories
    make_dir_in(d, "d1");
    write_text(d, "d1\\f.txt", "F");
    run_tool(d, "mv", "d1 d2", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "d1"));
    expect_content(d, "d2\\f.txt", "F");
    run_result_free(&r);

    make_dir_in(d, "d3");
    run_tool(d, "mv", "d2 d3", NULL, &r);
    expect_content(d, "d3\\d2\\f.txt", "F");
    run_result_free(&r);

    run_tool(d, "mv", "d3 d3\\d2", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot move 'd3' to a subdirectory of itself") != NULL);
    run_result_free(&r);

    write_text(d, "file.txt", "f");
    run_tool(d, "mv", "d3 file.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot overwrite non-directory 'file.txt' with directory 'd3'") != NULL);
    run_result_free(&r);

    // several sources, target directory option
    make_dir_in(d, "target");
    write_text(d, "m1.txt", "1");
    write_text(d, "m2.txt", "2");
    run_tool(d, "mv", "m1.txt m2.txt target", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "target\\m1.txt", "1");
    expect_content(d, "target\\m2.txt", "2");
    run_result_free(&r);

    make_dir_in(d, "target2");
    write_text(d, "m3.txt", "3");
    run_tool(d, "mv", "-t target2 m3.txt", NULL, &r);
    expect_content(d, "target2\\m3.txt", "3");
    run_result_free(&r);

    write_text(d, "m4.txt", "4");
    write_text(d, "m5.txt", "5");
    run_tool(d, "mv", "m4.txt m5.txt file.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "mv: target 'file.txt' is not a directory") != NULL);
    run_result_free(&r);

    // errors
    run_tool(d, "mv", "nosuch.txt z.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "mv: cannot stat 'nosuch.txt': No such file or directory") != NULL);
    run_result_free(&r);

    run_tool(d, "mv", "m4.txt m4.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "'m4.txt' and 'm4.txt' are the same file") != NULL);
    run_result_free(&r);

    run_tool(d, "mv", "m4.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "missing destination file operand after 'm4.txt'") != NULL);
    run_result_free(&r);

    // names that differ only in case are one file to Windows, but a rename is fine
    write_text(d, "case.txt", "c");
    run_tool(d, "mv", "case.txt CASE.TXT", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    run_tool(d, "ls", "", NULL, &r);
    CHECK(has_line(r.out, "CASE.TXT"));
    CHECK(!has_line(r.out, "case.txt"));
    run_result_free(&r);

    run_tool(d, "mv", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: mv") != NULL);
    run_result_free(&r);
}

int main(void) {
    tools_init("fileops_test");

    test_rm();
    test_cp();
    test_mv();
    return test_summary("fileops_test");
}
