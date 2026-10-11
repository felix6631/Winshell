/*
 * Tests of ls: sorting, hidden names, the long format, the virtual root.
 */

#include "testtools.h"

/** Runs ls with arguments in the work directory and compares the output, which is LF only. */
static void ls_expect(const char* arguments, const char* expected) {
    RunResult r;
    run_command("ls", arguments, NULL, &r);
    CHECK_STR(r.out, expected);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
}

static void test_ls(void) {
    RunResult r;
    char dir[MAX_PATH * 2], line[MAX_PATH * 3], windowsDir[MAX_PATH], drive[4];

    printf("ls\n");

    snprintf(dir, sizeof dir, "%s\\lsdir", workDir);
    CreateDirectoryA(dir, NULL);
    snprintf(line, sizeof line, "%s\\lsdir\\sub", workDir);
    CreateDirectoryA(line, NULL);
    make_sized_file(dir, "a.txt", 3, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(dir, "B.txt", 10, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(dir, "big.bin", 1500, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(dir, "ro.txt", 1, FILE_ATTRIBUTE_READONLY);
    make_sized_file(dir, "Zed.exe", 100, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(dir, ".dotfile", 1, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(dir, "hid.dat", 1, FILE_ATTRIBUTE_HIDDEN);
    snprintf(line, sizeof line, "%s\\sub", dir);
    make_sized_file(line, "inner.txt", 5, FILE_ATTRIBUTE_NORMAL);

    // names are sorted without regard to case; hidden ones are not shown
    ls_expect("lsdir", "a.txt\nB.txt\nbig.bin\nro.txt\nsub\nZed.exe\n");
    ls_expect("-1 lsdir", "a.txt\nB.txt\nbig.bin\nro.txt\nsub\nZed.exe\n");
    ls_expect("-r lsdir", "Zed.exe\nsub\nro.txt\nbig.bin\nB.txt\na.txt\n");

    // dot files and files with the hidden attribute
    ls_expect("-a lsdir", ".\n..\n.dotfile\na.txt\nB.txt\nbig.bin\nhid.dat\nro.txt\nsub\nZed.exe\n");
    ls_expect("-A lsdir", ".dotfile\na.txt\nB.txt\nbig.bin\nhid.dat\nro.txt\nsub\nZed.exe\n");

    // sorting by size, largest first
    ls_expect("-S lsdir", "big.bin\nZed.exe\nB.txt\na.txt\nro.txt\nsub\n");

    // indicators
    ls_expect("-F lsdir", "a.txt\nB.txt\nbig.bin\nro.txt\nsub/\nZed.exe*\n");
    ls_expect("-p lsdir", "a.txt\nB.txt\nbig.bin\nro.txt\nsub/\nZed.exe\n");

    // directories as themselves, and files given by name
    ls_expect("-d lsdir", "lsdir\n");
    ls_expect("lsdir/a.txt", "lsdir/a.txt\n");
    ls_expect("lsdir/sub", "inner.txt\n");

    // several operands: files first, then every directory with a header
    ls_expect("lsdir/a.txt lsdir/sub", "lsdir/a.txt\n\nlsdir/sub:\ninner.txt\n");

    // recursion
    ls_expect("-R lsdir", "lsdir:\na.txt\nB.txt\nbig.bin\nro.txt\nsub\nZed.exe\n\nlsdir/sub:\ninner.txt\n");

    // virtual paths
    snprintf(line, sizeof line, "%s/lsdir/sub", virtualWorkDir);
    ls_expect(line, "inner.txt\n");

    // the long format
    run_command("ls", "-l lsdir", NULL, &r);
    CHECK(strstr(r.out, "total ") == r.out);
    CHECK(strstr(r.out, "\n-rw-r--r-- 1 ") != NULL);
    CHECK(strstr(r.out, "\n-r--r--r-- 1 ") != NULL);           // read-only
    CHECK(strstr(r.out, "\ndrwxr-xr-x 1 ") != NULL);           // directory
    CHECK(strstr(r.out, "\n-rwxr-xr-x 1 ") != NULL);           // program
    CHECK(strstr(r.out, " a.txt\n") != NULL && strstr(r.out, " Zed.exe\n") != NULL);
    CHECK(strstr(r.out, "hid.dat") == NULL && strstr(r.out, ".dotfile") == NULL);
    // sizes are right aligned to the widest one
    CHECK(strstr(r.out, " 1500 ") != NULL);
    CHECK(strstr(r.out, "    3 ") != NULL);
    run_result_free(&r);

    run_command("ls", "-lh lsdir", NULL, &r);
    CHECK(strstr(r.out, " 1.5K ") != NULL);
    CHECK(strstr(r.out, " 100 ") != NULL);
    run_result_free(&r);

    // modification times: -t newest first, and the year for an old file
    set_mtime(dir, "a.txt", 2001, 6, 15);
    set_mtime(dir, "B.txt", 2005, 6, 15);
    set_mtime(dir, "big.bin", 2003, 6, 15);
    set_mtime(dir, "ro.txt", 2002, 6, 15);
    set_mtime(dir, "Zed.exe", 2004, 6, 15);
    ls_expect("-t lsdir", "sub\nB.txt\nZed.exe\nbig.bin\nro.txt\na.txt\n");
    ls_expect("-tr lsdir", "a.txt\nro.txt\nbig.bin\nZed.exe\nB.txt\nsub\n");
    run_command("ls", "-l lsdir/a.txt", NULL, &r);
    CHECK(strstr(r.out, " Jun 15  2001 lsdir/a.txt\n") != NULL);
    run_result_free(&r);

    // the virtual root holds the drives
    GetWindowsDirectoryA(windowsDir, sizeof windowsDir);
    drive[0] = (char)((windowsDir[0] >= 'A' && windowsDir[0] <= 'Z') ? windowsDir[0] + 32 : windowsDir[0]);
    drive[1] = '\0';
    run_command("ls", "/", NULL, &r);
    CHECK(has_line(r.out, drive));
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_command("ls", "-a /", NULL, &r);
    CHECK(strstr(r.out, ".\n..\n") == r.out);
    run_result_free(&r);

    snprintf(line, sizeof line, "/%s", drive);
    run_command("ls", line, NULL, &r);
    CHECK(has_line(r.out, "Windows") || has_line(r.out, "windows") || has_line(r.out, "WINDOWS"));
    run_result_free(&r);

    // errors: nothing to access is an exit code of 2
    run_command("ls", "nosuch lsdir/a.txt", NULL, &r);
    CHECK(strstr(r.err, "ls: cannot access 'nosuch': No such file or directory") != NULL);
    CHECK_STR(lf_only(r.out), "lsdir/a.txt\n");
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);

    run_command("ls", "-x", NULL, &r);
    CHECK(strstr(r.err, "invalid option -- 'x'") != NULL);
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);

    run_command("ls", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: ls") != NULL);
    run_result_free(&r);

    run_command("ls", "--version", NULL, &r);
    CHECK(strstr(r.out, "ls (Winshell)") != NULL);
    run_result_free(&r);

    // the ignored colour option
    ls_expect("--color=auto lsdir/sub", "inner.txt\n");
}

int main(void) {
    tools_init("ls_test");

    test_ls();
    return test_summary("ls_test");
}
