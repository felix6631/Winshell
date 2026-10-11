/*
 * Tests of the shell, run as the program it is: a script goes in through standard
 * input and the output is checked. Running commands, quoting, pipes, redirection.
 * 
 * Not covered by the tests of the shell: Ctrl+C. It needs a real console to receive
 * the event, which a program with redirected handles does not have.
 */

#include "testtools.h"

static void test_running(void) {
    RunResult r;

    printf("running commands\n");

    // output arrives in the order of the commands: the shell waits for each of them
    SCRIPT_OUT("echo one\necho two\necho three\n", "one\ntwo\nthree\n");

    // the script may end without "quit", and without a last newline
    SCRIPT_OUT("echo end", "end\n");

    SCRIPT_OUT("echo before\nquit\necho after\n", "before\n");

    // blank lines do nothing
    SCRIPT_OUT("\n   \n\t\necho x\n", "x\n");

    // CR LF line endings of a script are fine
    SCRIPT_OUT("echo crlf\r\necho again\r\n", "crlf\nagain\n");

    // ".exe" may be given
    SCRIPT_OUT("echo.exe hi\n", "hi\n");

    // unknown programs are reported and do not stop the shell
    script("nosuchprogram arg\necho still here\n", &r);
    CHECK_STR(r.out, "still here\n");
    CHECK(strstr(r.err, "Failed to execute command 'nosuchprogram'") != NULL);
    run_result_free(&r);

    // programs are only run from the bin directory, not from the current one
    write_text(workDir, "local.bat", "@echo local\r\n");
    script("local\n", &r);
    CHECK(strstr(r.err, "Failed to execute command 'local'") != NULL);
    run_result_free(&r);

    // a very long argument list is passed on completely
    {
        char* line = malloc(64 * 1024);
        char* expected = malloc(64 * 1024);
        size_t l = 0, e = 0;
        int i;
        l += (size_t)sprintf(line, "echo");
        for (i = 1; i <= 2000; i++) {
            l += (size_t)sprintf(line + l, " w%d", i);
            e += (size_t)sprintf(expected + e, i == 1 ? "w%d" : " w%d", i);
        }
        strcpy(line + l, "\n");
        strcpy(expected + e, "\n");
        SCRIPT_OUT(line, expected);
        free(line);
        free(expected);
    }
}

static void test_quoting(void) {
    RunResult r;

    printf("quoting\n");

    // echo joins its arguments with one space, so spaces that survive were quoted
    SCRIPT_OUT("echo a    b\n", "a b\n");
    SCRIPT_OUT("echo \"a    b\"\n", "a    b\n");
    SCRIPT_OUT("echo 'a    b'\n", "a    b\n");
    SCRIPT_OUT("echo x\"a   b\"y\n", "xa   by\n");
    SCRIPT_OUT("echo \"say \\\"hi\\\"\"\n", "say \"hi\"\n");
    SCRIPT_OUT("echo 'single \"inside\"'\n", "single \"inside\"\n");
    SCRIPT_OUT("echo \"a|b>c<d\"\n", "a|b>c<d\n");

    // the empty argument is there
    SCRIPT_OUT("echo a \"\" b\n", "a  b\n");

    // unclosed quotes: error, nothing runs, the shell goes on
    script("echo \"open\necho next\n", &r);
    CHECK(strstr(r.err, "Unterminated double quote") != NULL);
    CHECK_STR(r.out, "next\n");
    run_result_free(&r);
}

static void test_pipes(void) {
    RunResult r;

    printf("pipes\n");

    SCRIPT_OUT("echo hello | cat\n", "hello\n");
    SCRIPT_OUT("echo hello | cat -n\n", "     1\thello\n");
    SCRIPT_OUT("echo hello | cat -n | cat -E\n", "     1\thello$\n");
    SCRIPT_OUT("echo hello|cat -n\n", "     1\thello\n");

    // a pipe is no file: much more than its buffer goes through
    {
        char* line = malloc(64 * 1024);
        size_t l = 0;
        int i;
        l += (size_t)sprintf(line, "echo");
        for (i = 0; i < 2500; i++)
            l += (size_t)sprintf(line + l, " 0123456789");
        strcpy(line + l, " | cat | cat\n");
        script(line, &r);
        CHECK_INT(strlen(r.out), 2500 * 11);   // the words, their separators, and the line feed
        CHECK(!r.timedOut);
        run_result_free(&r);

        // beyond the 32767 characters Windows allows for a command line: reported, shell goes on
        l = (size_t)sprintf(line, "echo");
        for (i = 0; i < 3500; i++)
            l += (size_t)sprintf(line + l, " 0123456789");
        strcpy(line + l, "\necho after\n");
        script(line, &r);
        CHECK(strstr(r.err, "Command line is too long") != NULL);
        CHECK_STR(r.out, "after\n");
        run_result_free(&r);
        free(line);
    }

    // a command that cannot start still lets the others end
    script("nosuchprogram | cat\necho alive\n", &r);
    CHECK_STR(r.out, "alive\n");
    CHECK(!r.timedOut);
    run_result_free(&r);

    script("echo x | nosuchprogram\necho alive\n", &r);
    CHECK_STR(r.out, "alive\n");
    CHECK(!r.timedOut);
    run_result_free(&r);

    // syntax errors
    script("echo hi |\necho next\n", &r);
    CHECK(strstr(r.err, "missing command after '|'") != NULL);
    CHECK_STR(r.out, "next\n");
    run_result_free(&r);

    script("| cat\n", &r);
    CHECK(strstr(r.err, "missing command before '|'") != NULL);
    run_result_free(&r);
}

static void test_redirection(void) {
    RunResult r;
    char* text;

    printf("redirection\n");

    // > creates and replaces, >> appends
    SCRIPT_OUT("echo first > out.txt\necho second > out.txt\ncat out.txt\n", "second\n");
    SCRIPT_OUT("echo first > app.txt\necho second >> app.txt\ncat app.txt\n", "first\nsecond\n");
    SCRIPT_OUT("echo hi>nospace.txt\ncat nospace.txt\n", "hi\n");

    // the file really holds the output, and nothing reached the screen
    script("echo captured > cap.txt\n", &r);
    CHECK_STR(r.out, "");
    run_result_free(&r);
    text = read_file(workDir, "cap.txt", NULL);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK_STR(lf_only(text), "captured\n");
        free(text);
    }

    // <
    write_text(workDir, "in.txt", "one\ntwo\n");
    SCRIPT_OUT("cat < in.txt\n", "one\ntwo\n");
    SCRIPT_OUT("cat -n < in.txt > numbered.txt\ncat numbered.txt\n", "     1\tone\n     2\ttwo\n");
    SCRIPT_OUT("cat < in.txt | cat -n\n", "     1\tone\n     2\ttwo\n");

    // 2>
    script("cat nosuch.txt 2> err.txt\ncat err.txt\n", &r);
    CHECK_STR(r.err, "");
    CHECK(strstr(r.out, "cat: nosuch.txt: No such file or directory") != NULL);
    run_result_free(&r);

    script("cat nosuch.txt 2>> err2.txt\ncat nosuch2.txt 2>> err2.txt\ncat err2.txt\n", &r);
    CHECK(r.out != NULL && strstr(r.out, "nosuch.txt") != NULL && strstr(r.out, "nosuch2.txt") != NULL);
    run_result_free(&r);

    // names with spaces
    SCRIPT_OUT("echo spaced > \"with space.txt\"\ncat \"with space.txt\"\n", "spaced\n");

    // a missing input file is reported and the shell goes on
    script("cat < nosuch.txt\necho next\n", &r);
    CHECK(strstr(r.err, "Cannot open 'nosuch.txt' for reading") != NULL);
    CHECK_STR(r.out, "next\n");
    run_result_free(&r);

    // a missing file name is a syntax error
    script("echo hi >\n", &r);
    CHECK(strstr(r.err, "file name expected after '>'") != NULL);
    run_result_free(&r);

    // file names are virtual paths
    {
        char line[MAX_PATH * 3];
        snprintf(line, sizeof line, "echo virtual > %s/virt.txt\ncat %s/virt.txt\n", virtualWorkDir, virtualWorkDir);
        SCRIPT_OUT(line, "virtual\n");
        text = read_file(workDir, "virt.txt", NULL);
        CHECK(text != NULL);
        if (text != NULL) {
            CHECK_STR(lf_only(text), "virtual\n");
            free(text);
        }
    }
}

int main(void) {
    tools_init("shell_run_test");

    test_running();
    test_quoting();
    test_pipes();
    test_redirection();
    return test_summary("shell_run_test");
}
