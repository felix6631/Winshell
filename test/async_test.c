/*
 * Programs that go on running while the test goes on: input that arrives in pieces, a program that
 * has to wait for the end of its input, a follower of a file, and programs that stop reading early.
 * These use Process of testlib.h: started, written to, looked at, then ended or waited for.
 */

#include "testtools.h"

#define WAIT 5000

static void start(Process* p, const char* tool, const char* arguments) {
    char path[MAX_PATH * 2];
    bin_path(tool, path, sizeof path);
    CHECK(proc_start(p, path, arguments, workDir) == 0);
}

/** A filter that works line by line shows its output before the input is closed; one that has to sort does not. */
static void test_streaming(void) {
    Process p;
    RunResult r;
    char* output;

    printf("input that comes in pieces\n");

    // cat passes on what it reads as it comes: the first line is out before the input is closed
    start(&p, "cat", "");
    CHECK(proc_write(&p, "first\n", 6) == 0);
    CHECK(proc_wait_for_output(&p, "first", WAIT));
    CHECK(!proc_finished(&p));
    CHECK(proc_write(&p, "second\r\n", 8) == 0);
    CHECK(proc_wait_for_output(&p, "second", WAIT));
    proc_close_input(&p);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_STR(r.out, "first\nsecond\r\n");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    // so does tr, in the middle of a pipeline
    start(&p, "tr", "a-z A-Z");
    CHECK(proc_write(&p, "abc\n", 4) == 0);
    CHECK(proc_wait_for_output(&p, "ABC", WAIT));
    proc_close_input(&p);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_STR(r.out, "ABC\n");
    run_result_free(&r);

    // and tail -n +2, which does not have to know where the end is
    start(&p, "tail", "-n +2");
    CHECK(proc_write(&p, "skipped\nkept\n", 13) == 0);
    CHECK(proc_wait_for_output(&p, "kept", WAIT));
    CHECK(!proc_finished(&p));
    proc_kill(&p, &r);
    run_result_free(&r);

    // sort needs all of its input: it is not finished before the end of it, and then it is
    start(&p, "sort", "");
    CHECK(proc_write(&p, "pear\n", 5) == 0);
    CHECK(proc_write(&p, "apple\n", 6) == 0);
    Sleep(200);
    CHECK(!proc_finished(&p));
    output = proc_output(&p, NULL);
    CHECK_STR(output, "");
    free(output);
    CHECK(proc_write(&p, "fig", 3) == 0);               // a last line without a line feed
    proc_close_input(&p);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_STR(r.out, "apple\nfig\npear\n");
    run_result_free(&r);

    // wc counts what came, over several writes, with a line that is cut between them
    start(&p, "wc", "-lwc");
    CHECK(proc_write(&p, "one tw", 6) == 0);
    Sleep(30);
    CHECK(proc_write(&p, "o three\nfour", 12) == 0);
    proc_close_input(&p);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_STR(lf_only(r.out), "      1       4      18\n");
    run_result_free(&r);

    // a lot of input in small pieces
    {
        int i;
        start(&p, "wc", "-l");
        for (i = 0; i < 2000; i++)
            CHECK(proc_write(&p, "line\n", 5) == 0);
        proc_close_input(&p);
        CHECK(proc_wait(&p, WAIT, &r) == 0);
        CHECK_STR(lf_only(r.out), "2000\n");
        run_result_free(&r);
    }
}

/** head and tail do not need the end of their input when they have what they were asked for. */
static void test_early_exit(void) {
    Process p;
    RunResult r;

    printf("programs that stop reading early\n");

    start(&p, "head", "-n 2");
    CHECK(proc_write(&p, "one\ntwo\nthree\n", 14) == 0);
    // its input is still open: head has had its two lines and is done
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_STR(lf_only(r.out), "one\ntwo\n");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    start(&p, "head", "-c 3");
    CHECK(proc_write(&p, "abcdef", 6) == 0);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_STR(r.out, "abc");
    run_result_free(&r);
}

/** tail -f follows a file: what is appended appears, a truncation is reported. */
static void test_follow(void) {
    char d[MAX_PATH * 2], path[MAX_PATH * 2];
    Process p;
    RunResult r;
    FILE* f;

    printf("tail -f\n");
    snprintf(d, sizeof d, "%s\\follow", workDir);
    reset_dir(d);
    write_text(d, "log.txt", "old1\nold2\nold3\n");
    snprintf(path, sizeof path, "%s\\log.txt", d);

    {
        char program[MAX_PATH * 2], arguments[MAX_PATH * 3];
        bin_path("tail", program, sizeof program);
        snprintf(arguments, sizeof arguments, "-f -n 2 -s 0.1 \"%s\"", path);
        CHECK(proc_start(&p, program, arguments, d) == 0);
    }

    // the last two lines first
    CHECK(proc_wait_for_output(&p, "old3", WAIT));
    {
        char* o = proc_output(&p, NULL);
        CHECK(strstr(o, "old1") == NULL);
        free(o);
    }

    // appended, with both kinds of line ending
    f = fopen(path, "ab");
    fputs("new1\r\nnew2\n", f);
    fclose(f);
    CHECK(proc_wait_for_output(&p, "new2", WAIT));

    // appended again, in pieces
    f = fopen(path, "ab");
    fputs("par", f);
    fflush(f);
    Sleep(250);
    fputs("tial\n", f);
    fclose(f);
    CHECK(proc_wait_for_output(&p, "partial", WAIT));

    // cut short: reported, and the new content is shown from its start
    write_text(d, "log.txt", "fresh\n");
    CHECK(proc_wait_for_output(&p, "fresh", WAIT));
    CHECK(!proc_finished(&p));

    proc_kill(&p, &r);
    CHECK(strstr(r.out, "old2\nold3\n") != NULL);
    CHECK(strstr(r.out, "new1\r\nnew2\n") != NULL);
    CHECK(strstr(r.out, "partial\n") != NULL);
    CHECK(strstr(r.err, "file truncated") != NULL);
    run_result_free(&r);
}

/** Two programs of a pipeline that run at the same time, and the shell waiting for both. */
static void test_shell_waits(void) {
    Process p;
    RunResult r;

    printf("the shell and a program that waits\n");

    // the shell runs sort, which needs its end of input; the script gives it and then asks for more
    {
        char program[MAX_PATH * 2];
        bin_path("shell", program, sizeof program);
        CHECK(proc_start(&p, program, "", workDir) == 0);
    }
    CHECK(proc_write(&p, "echo b a | sort\n", 16) == 0);
    CHECK(proc_wait_for_output(&p, "b a", WAIT));
    CHECK(!proc_finished(&p));              // the shell waits for its next line
    CHECK(proc_write(&p, "echo done\n", 10) == 0);
    CHECK(proc_wait_for_output(&p, "done", WAIT));
    CHECK(proc_write(&p, "exit 4\n", 7) == 0);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_INT(r.exitCode, 4);
    run_result_free(&r);
}

int main(void) {
    tools_init("async_test");
    test_streaming();
    test_early_exit();
    test_follow();
    test_shell_waits();
    return test_summary("async_test");
}
