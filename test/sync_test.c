/*
 * Tests of sync, run as the program it is: files and directories are flushed or reported, and the option errors.
 * GNU sync is compared with by gnu_compare.sh.
 */

#include "testtools.h"

int main(void) {
    tools_init("sync_test");
    reset_dir(workDir);
    write_text(workDir, "f.txt", "data\n");
    make_directory("d");

    printf("sync\n");
    expect_run("sync", "", "", "", 0);                                  // everything: nothing to say
    expect_run("sync", "f.txt", "", "", 0);
    expect_run("sync", "d", "", "", 0);                                 // a directory has nothing to flush
    expect_run("sync", "f.txt d f.txt", "", "", 0);
    expect_run("sync", "-d f.txt", "", "", 0);
    expect_run("sync", "--data f.txt", "", "", 0);
    expect_run("sync", "-f f.txt", "", "", 0);
    expect_run("sync", "--file-system", "", "", 0);                     // without a file it is allowed
    expect_content(workDir, "f.txt", "data\n");

    printf("errors\n");
    expect_run("sync", "nosuchfile", "", "", 1);
    expect_run("sync", "f.txt nosuchfile", "", "", 1);                  // the others are still done
    expect_run("sync", "-d", "", "", 1);                                // --data needs an argument
    expect_run("sync", "-d -f f.txt", "", "", 1);
    expect_run("sync", "--bogus", "", "", 1);
    expect_run("sync", "--version", "", "sync (Winshell) 0.1\n", 0);
    return test_summary("sync_test");
}
