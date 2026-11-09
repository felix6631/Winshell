/*
 * Tests of install, run as the program it is: copying a file (the old one is replaced), into a directory, -t and -T, -D and
 * -d (directories, with -v), -m (the read-only attribute is all of the mode Windows keeps), backups, -C, -p and the errors.
 * GNU install is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* expected, int status) {
    expect_run("install", arguments, "", expected, status);
}

static void message(const char* arguments, const char* part, int status) {
    RunResult r;

    run_tool(workDir, "install", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

static void fresh(void) {
    reset_dir(workDir);
    write_text(workDir, "a", "hello\n");
    write_text(workDir, "a2", "other\n");
    make_directory("dd");
    write_text(workDir, "b", "old b\n");
}

static void mode_is(const char* name, const char* expected) {
    RunResult r;
    char arguments[128];

    snprintf(arguments, sizeof arguments, "-c %%a %s", name);
    run_tool(workDir, "stat", arguments, "", &r);
    CHECK(r.out != NULL && strncmp(r.out, expected, strlen(expected)) == 0 && r.out[strlen(expected)] == '\n');
    run_result_free(&r);
}

int main(void) {
    tools_init("install_test");
    fresh();

    printf("copying\n");
    expect("a c", "", 0);
    expect_content(workDir, "c", "hello\n");
    expect("-v a c", "removed 'c'\n'a' -> 'c'\n", 0);                      // the old file is removed first
    expect("-v a b", "removed 'b'\n'a' -> 'b'\n", 0);
    expect_content(workDir, "b", "hello\n");
    expect("-v a a2 dd", "'a' -> 'dd/a'\n'a2' -> 'dd/a2'\n", 0);
    expect_content(workDir, "dd\\a2", "other\n");
    expect("-v -t dd a", "removed 'dd/a'\n'a' -> 'dd/a'\n", 0);
    expect("-v a dd/", "removed 'dd/a'\n'a' -> 'dd/a'\n", 0);
    expect("-T a c", "", 0);
    message("-T a dd", "cannot overwrite directory 'dd' with non-directory", 1);
    message("-T a a2 c", "extra operand 'c'", 1);

    printf("modes\n");
    fresh();
    expect("a m1", "", 0);
    mode_is("m1", "644");                                                 // 0755 asked for: not a program, so 644 is what stat shows
    expect("-m 444 a m2", "", 0);
    mode_is("m2", "444");
    expect("-m 0 a m3", "", 0);
    mode_is("m3", "444");
    expect("-m u+x a m4", "", 0);
    mode_is("m4", "444");
    expect("-m g+w a m5", "", 0);
    mode_is("m5", "644");
    expect("-m 444 a m2", "", 0);                                         // a read-only destination is replaced
    message("-m 999 a c", "invalid mode '999'", 1);
    message("-m xyz a c", "invalid mode 'xyz'", 1);

    printf("directories\n");
    fresh();
    expect("-d x", "", 0);
    expect("-d -v x/y/z", "install: creating directory 'x/y'\ninstall: creating directory 'x/y/z'\n", 0);
    expect("-d -v dd", "", 0);                                            // already there
    expect("-D -v a q/r/s", "install: creating directory 'q'\ninstall: creating directory 'q/r'\n'a' -> 'q/r/s'\n", 0);
    expect("-D -v -t t1/t2 a", "install: creating directory 't1'\ninstall: creating directory 't1/t2'\n'a' -> 't1/t2/a'\n", 0);
    message("a nosuch/c", "cannot create regular file 'nosuch/c': No such file or directory", 1);
    message("-t nosuch a", "failed to access 'nosuch': No such file or directory", 1);
    message("-d a/b", "cannot create directory 'a': Not a directory", 1);
    message("-d a", "cannot create directory 'a': File exists", 1);

    printf("backups\n");
    fresh();
    expect("-b -v a b", "'a' -> 'b' (backup: 'b~')\n", 0);
    expect_content(workDir, "b~", "old b\n");
    expect("--backup=numbered -v a b", "'a' -> 'b' (backup: 'b.~1~')\n", 0);
    expect("--backup=numbered -v a b", "'a' -> 'b' (backup: 'b.~2~')\n", 0);
    expect("-b -v a b", "'a' -> 'b' (backup: 'b.~3~')\n", 0);             // existing: numbered ones are there
    expect("--backup=simple -S .orig -v a b", "'a' -> 'b' (backup: 'b.orig')\n", 0);
    expect("--backup=none -v a b", "removed 'b'\n'a' -> 'b'\n", 0);
    message("--backup=bogus a b", "invalid argument 'bogus' for 'backup type'", 1);

    printf("-C, -p and others\n");
    fresh();
    expect("-m 644 a c", "", 0);
    expect("-C -v -m 644 a c", "", 0);                                    // the same content and mode: left alone
    write_text(workDir, "c", "changed\n");
    expect("-C -v -m 644 a c", "removed 'c'\n'a' -> 'c'\n", 0);
    expect("-C -v a c", "removed 'c'\n'a' -> 'c'\n", 0);                  // another mode: copied
    set_mtime(workDir, "a", 2001, 2, 3);
    expect("-p a pt", "", 0);
    CHECK_INT(year_of(workDir, "pt"), 2001);
    expect("-c a c2", "", 0);
    expect("-Z a c3", "", 0);

    printf("errors\n");
    message("", "missing file operand", 1);
    message("a", "missing destination file operand after 'a'", 1);
    message("nosuch c", "cannot stat 'nosuch': No such file or directory", 1);
    message("dd e", "omitting directory 'dd'", 1);
    message("a a2 b", "target 'b' is not a directory", 1);
    message("a a", "'a' and 'a' are the same file", 1);
    message("-o nosuchuser_zz a c", "invalid user 'nosuchuser_zz'", 1);
    message("-g nosuchgroup_zz a c", "invalid group 'nosuchgroup_zz'", 1);
    message("--bogus a c", "unrecognized option", 1);
    expect_run("install", "--version", "", "install (Winshell) 0.1\n", 0);
    return test_summary("install_test");
}
