/*
 * Tests of mkfifo and mknod, run as the programs they are: Windows file systems hold no FIFOs and no device files, so no file
 * is made; the options and the operands are checked as GNU checks them, and every name ends with the reason of the file
 * system. GNU mkfifo and mknod are compared with by gnu_compare.sh (the errors that do not depend on the file being made).
 */

#include "testtools.h"

static void message(const char* tool, const char* arguments, const char* expectedError, int status) {
    RunResult r;

    run_tool(workDir, tool, arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    CHECK_STR(r.out, "");
    lf_only(r.err);
    if (strcmp(r.err, expectedError) != 0)
        printf("    for: %s %s\n    got: \"%s\"\n", tool, arguments, r.err);
    CHECK_STR(r.err, expectedError);
    run_result_free(&r);
}

int main(void) {
    tools_init("mkfifo_test");
    reset_dir(workDir);
    write_text(workDir, "exists.txt", "x");
    make_directory("dir");

    printf("mkfifo\n");
    message("mkfifo", "", "mkfifo: missing operand\nTry 'mkfifo --help' for more information.\n", 1);
    message("mkfifo", "-m 755", "mkfifo: missing operand\nTry 'mkfifo --help' for more information.\n", 1);
    message("mkfifo", "-m", "mkfifo: option requires an argument -- 'm'\nTry 'mkfifo --help' for more information.\n", 1);
    message("mkfifo", "-m zzz f", "mkfifo: invalid mode\n", 1);
    message("mkfifo", "f", "mkfifo: cannot create fifo 'f': Operation not supported\n", 1);
    message("mkfifo", "-m 600 f", "mkfifo: cannot create fifo 'f': Operation not supported\n", 1);
    message("mkfifo", "-m u=rw,go= f", "mkfifo: cannot create fifo 'f': Operation not supported\n", 1);
    message("mkfifo", "-Z f", "mkfifo: cannot create fifo 'f': Operation not supported\n", 1);
    message("mkfifo", "exists.txt", "mkfifo: cannot create fifo 'exists.txt': File exists\n", 1);
    message("mkfifo", "dir", "mkfifo: cannot create fifo 'dir': File exists\n", 1);
    message("mkfifo", "nosuchdir/f", "mkfifo: cannot create fifo 'nosuchdir/f': No such file or directory\n", 1);
    message("mkfifo", "exists.txt/f", "mkfifo: cannot create fifo 'exists.txt/f': Not a directory\n", 1);
    message("mkfifo", "dir/f", "mkfifo: cannot create fifo 'dir/f': Operation not supported\n", 1);
    message("mkfifo", "a exists.txt b",
            "mkfifo: cannot create fifo 'a': Operation not supported\nmkfifo: cannot create fifo 'exists.txt': File exists\n"
            "mkfifo: cannot create fifo 'b': Operation not supported\n", 1);
    message("mkfifo", "--context=x f",
            "mkfifo: warning: ignoring --context; it requires an SELinux/SMACK-enabled kernel\n"
            "mkfifo: cannot create fifo 'f': Operation not supported\n", 1);
    CHECK(!path_exists_in(workDir, "f"));                                    // nothing was made
    expect_run("mkfifo", "--version", "", "mkfifo (Winshell) 0.1\n", 0);

    printf("mknod\n");
    message("mknod", "", "mknod: missing operand\nTry 'mknod --help' for more information.\n", 1);
    message("mknod", "x", "mknod: missing operand after 'x'\nTry 'mknod --help' for more information.\n", 1);
    message("mknod", "x b",
            "mknod: missing operand after 'b'\nSpecial files require major and minor device numbers.\n"
            "Try 'mknod --help' for more information.\n", 1);
    message("mknod", "x q",
            "mknod: missing operand after 'q'\nSpecial files require major and minor device numbers.\n"
            "Try 'mknod --help' for more information.\n", 1);
    message("mknod", "x b 1", "mknod: missing operand after '1'\nTry 'mknod --help' for more information.\n", 1);
    message("mknod", "x c 1 2 3", "mknod: extra operand '3'\nTry 'mknod --help' for more information.\n", 1);
    message("mknod", "x p 1 2",
            "mknod: extra operand '1'\nFifos do not have major and minor device numbers.\nTry 'mknod --help' for more information.\n", 1);
    message("mknod", "x p extra", "mknod: extra operand 'extra'\nTry 'mknod --help' for more information.\n", 1);
    message("mknod", "x q 1 2", "mknod: invalid device type 'q'\nTry 'mknod --help' for more information.\n", 1);
    message("mknod", "x c 1 y", "mknod: invalid minor device number 'y'\n", 1);
    message("mknod", "x c z 1", "mknod: invalid major device number 'z'\n", 1);
    message("mknod", "x c 99999999999999999999 1", "mknod: invalid major device number '99999999999999999999'\n", 1);
    message("mknod", "-m zzz x p", "mknod: invalid mode\n", 1);
    message("mknod", "x p", "mknod: x: Operation not supported\n", 1);
    message("mknod", "-m 644 x p", "mknod: x: Operation not supported\n", 1);
    message("mknod", "x b 8 0", "mknod: x: Operation not supported\n", 1);
    message("mknod", "x c 0x1 010", "mknod: x: Operation not supported\n", 1);
    message("mknod", "exists.txt u 1 2", "mknod: exists.txt: File exists\n", 1);
    message("mknod", "nosuchdir/x p", "mknod: nosuchdir/x: No such file or directory\n", 1);
    CHECK(!path_exists_in(workDir, "x"));
    expect_run("mknod", "--version", "", "mknod (Winshell) 0.1\n", 0);
    return test_summary("mkfifo_test");
}
