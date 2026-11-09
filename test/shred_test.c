/*
 * Tests of shred, run as the program it is: a file is overwritten (its size rounded up to 4096 unless -x or -s), -z
 * leaves zeros, -u removes it, -v tells the passes, and the errors. GNU shred is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, int status) {
    expect_run("shred", arguments, "", "", status);
}

static void message(const char* arguments, const char* part, int status) {
    RunResult r;

    run_tool(workDir, "shred", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

static long size_of(const char* name) {
    char* text = read_file(workDir, name, NULL);
    RunResult r;
    char arguments[128];
    long size;

    (void)text;
    free(text);
    snprintf(arguments, sizeof arguments, "-c %%s %s", name);
    run_tool(workDir, "stat", arguments, "", &r);
    size = r.out != NULL ? atol(r.out) : -1;
    run_result_free(&r);
    return size;
}

int main(void) {
    char* text;
    size_t i, length = 0;
    int allZero = 1;

    tools_init("shred_test");
    reset_dir(workDir);
    write_text(workDir, "f.txt", "sixteen bytes!!\n");

    printf("overwriting\n");
    expect("-x f.txt", 0);
    text = read_file(workDir, "f.txt", &length);
    CHECK(text != NULL && length == 16 && memcmp(text, "sixteen bytes!!\n", 16) != 0);   // the content is gone
    free(text);
    write_text(workDir, "f.txt", "sixteen bytes!!\n");
    expect("f.txt", 0);
    CHECK_INT((int)size_of("f.txt"), 4096);                                        // rounded up to a block
    write_text(workDir, "f.txt", "sixteen bytes!!\n");
    expect("-s 4 f.txt", 0);
    text = read_file(workDir, "f.txt", &length);
    CHECK(text != NULL && length == 16 && memcmp(text + 4, "een bytes!!\n", 12) == 0);   // only 4 bytes were shredded
    free(text);
    write_text(workDir, "f.txt", "sixteen bytes!!\n");
    expect("-xz -n 1 f.txt", 0);
    text = read_file(workDir, "f.txt", &length);
    for (i = 0; text != NULL && i < length; i++) {
        if (text[i] != 0)
            allZero = 0;
    }
    CHECK(text != NULL && length == 16 && allZero);                                  // the last pass is zeros
    free(text);
    write_text(workDir, "f.txt", "sixteen bytes!!\n");
    expect("-n 0 -x f.txt", 0);
    expect_content(workDir, "f.txt", "sixteen bytes!!\n");                          // no pass, nothing changed
    write_text(workDir, "e.txt", "");
    expect("e.txt", 0);                                                              // an empty file has nothing to shred
    CHECK_INT((int)size_of("e.txt"), 0);

    printf("verbose\n");
    message("-v -n 2 -x f.txt", "shred: f.txt: pass 1/2 (random)...", 0);
    message("-v -n 2 -x f.txt", "shred: f.txt: pass 2/2 (random)...", 0);
    message("-v -n 1 -xz f.txt", "shred: f.txt: pass 2/2 (000000)...", 0);

    printf("removing\n");
    write_text(workDir, "g.txt", "abc");
    expect("-u -x g.txt", 0);
    CHECK(!path_exists_in(workDir, "g.txt"));
    write_text(workDir, "g.txt", "abc");
    message("-vux g.txt", "shred: g.txt: removed", 0);
    CHECK(!path_exists_in(workDir, "g.txt"));
    write_text(workDir, "g.txt", "abc");
    message("-v --remove=wipe -x g.txt", "shred: g.txt: renamed to 00000", 0);
    CHECK(!path_exists_in(workDir, "g.txt") && !path_exists_in(workDir, "00000"));
    write_text(workDir, "g.txt", "abc");
    expect("--remove=unlink -x g.txt", 0);
    CHECK(!path_exists_in(workDir, "g.txt"));
    write_text(workDir, "g.txt", "abc");
    expect("-x g.txt", 0);
    CHECK(path_exists_in(workDir, "g.txt"));                                         // kept without -u

    printf("errors\n");
    message("nosuchfile", "shred: nosuchfile: failed to open for writing: No such file or directory", 1);
    make_directory("d");
    message("d", "failed to open for writing: Is a directory", 1);
    message("", "missing file operand", 1);
    message("-n x f.txt", "invalid number of passes: 'x'", 1);
    message("-s 2x f.txt", "invalid file size: '2x'", 1);
    message("--remove=bogus f.txt", "invalid argument 'bogus' for '--remove'", 1);
    message("--random-source=nosuchsource -x f.txt", "nosuchsource", 1);
    expect_run("shred", "--version", "", "shred (Winshell) 0.1\n", 0);
    return test_summary("shred_test");
}
