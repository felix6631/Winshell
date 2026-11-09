/*
 * Tests of chmod, run as the program it is: octal and symbolic modes (the messages tell the mode asked for), the read-only
 * attribute that is all Windows keeps (what stat shows), -R, --reference, -c / -v / -f and the errors. GNU chmod is
 * compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* expected, int status) {
    expect_run("chmod", arguments, "", expected, status);
}

static void message(const char* arguments, const char* part, int status) {
    RunResult r;

    run_tool(workDir, "chmod", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

/** The mode stat shows for a file. */
static void mode_is(const char* name, const char* expected) {
    RunResult r;
    char arguments[128];

    snprintf(arguments, sizeof arguments, "-c %%a %s", name);
    run_tool(workDir, "stat", arguments, "", &r);
    CHECK(r.out != NULL && strncmp(r.out, expected, strlen(expected)) == 0 && r.out[strlen(expected)] == '\n');
    run_result_free(&r);
}

int main(void) {
    tools_init("chmod_test");
    reset_dir(workDir);
    write_text(workDir, "f.txt", "x");
    make_directory("d");
    write_text(workDir, "d\\inner.txt", "y");
    write_text(workDir, "p.exe", "z");

    printf("modes and the read-only attribute\n");
    expect("-v 600 f.txt", "mode of 'f.txt' changed from 0644 (rw-r--r--) to 0600 (rw-------)\n", 0);
    mode_is("f.txt", "644");                                   // the file stays writable: only the write bit is kept
    expect("-v 444 f.txt", "mode of 'f.txt' changed from 0644 (rw-r--r--) to 0444 (r--r--r--)\n", 0);
    mode_is("f.txt", "444");
    expect("-v 444 f.txt", "mode of 'f.txt' retained as 0444 (r--r--r--)\n", 0);
    expect("-v u+w f.txt", "mode of 'f.txt' changed from 0444 (r--r--r--) to 0644 (rw-r--r--)\n", 0);
    mode_is("f.txt", "644");
    expect("a-w f.txt", "", 0);
    mode_is("f.txt", "444");
    expect("-w f.txt", "", 0);
    expect("g+w f.txt", "", 0);                               // any write bit makes the file writable
    mode_is("f.txt", "644");
    expect("u=rx,go= f.txt", "", 0);
    mode_is("f.txt", "444");
    expect("a=rw f.txt", "", 0);
    mode_is("f.txt", "644");
    expect("-v +x p.exe", "mode of 'p.exe' retained as 0755 (rwxr-xr-x)\n", 0);
    expect("-v 0 p.exe", "mode of 'p.exe' changed from 0755 (rwxr-xr-x) to 0000 (---------)\n", 0);
    mode_is("p.exe", "555");
    expect("644 p.exe", "", 0);

    printf("symbolic modes\n");
    expect("-v u=g f.txt", "mode of 'f.txt' changed from 0644 (rw-r--r--) to 0444 (r--r--r--)\n", 0);   // u takes what g has
    expect("-v ug=rwx,o=rx f.txt", "mode of 'f.txt' changed from 0444 (r--r--r--) to 0775 (rwxrwxr-x)\n", 0);
    expect("-v a=r,u+w f.txt", "mode of 'f.txt' retained as 0644 (rw-r--r--)\n", 0);

    printf("-c, -f, -R, --reference\n");
    expect("-c 644 f.txt", "", 0);                                // nothing changes: nothing is said
    expect("-c 600 f.txt", "mode of 'f.txt' changed from 0644 (rw-r--r--) to 0600 (rw-------)\n", 0);
    expect("-R 444 d", "", 0);
    mode_is("d\\inner.txt", "444");
    expect("-R 644 d", "", 0);
    mode_is("d\\inner.txt", "644");
    expect("--reference=p.exe f.txt", "", 0);
    write_text(workDir, "r.txt", "q");
    expect("444 r.txt", "", 0);
    expect("--reference=r.txt f.txt", "", 0);
    mode_is("f.txt", "444");
    expect("-f 644 nosuchfile", "", 1);                        // quiet, but still an error

    printf("errors\n");
    message("xyz f.txt", "invalid mode: 'xyz'", 1);
    message("999 f.txt", "invalid mode: '999'", 1);
    message("u+q f.txt", "invalid mode: 'u+q'", 1);
    message("644 nosuchfile", "cannot access 'nosuchfile': No such file or directory", 1);
    message("", "missing operand", 1);
    message("644", "missing operand after '644'", 1);
    message("--bogus 644 f.txt", "unrecognized option", 1);
    expect_run("chmod", "--version", "", "chmod (Winshell) 0.1\n", 0);
    return test_summary("chmod_test");
}
