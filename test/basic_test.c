/*
 * Tests of echo, arch and pwd, run as the programs they are, with their
 * arguments, standard input and exit codes.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void test_echo(void) {
    RunResult r;

    printf("echo\n");

    run_command("echo", "hello world", NULL, &r);
    CHECK_STR(lf_only(r.out), "hello world\n");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    // the words are joined by one space whatever separated them
    run_command("echo", "a    b", NULL, &r);
    CHECK_STR(lf_only(r.out), "a b\n");
    run_result_free(&r);

    run_command("echo", "", NULL, &r);
    CHECK_STR(lf_only(r.out), "\n");
    run_result_free(&r);

    run_command("echo", "-n no newline", NULL, &r);
    CHECK_STR(lf_only(r.out), "no newline");
    run_result_free(&r);

    // escapes: only with -e
    run_command("echo", "\"a\\tb\"", NULL, &r);
    CHECK_STR(lf_only(r.out), "a\\tb\n");
    run_result_free(&r);

    run_command("echo", "-e \"a\\tb\\\\c\\x41\\0102\"", NULL, &r);
    CHECK_STR(lf_only(r.out), "a\tb\\cAB\n");
    run_result_free(&r);

    run_command("echo", "-e \"stop\\cnot shown\"", NULL, &r);
    CHECK_STR(lf_only(r.out), "stop");
    run_result_free(&r);

    // -E switches escapes off again, options may be grouped
    run_command("echo", "-eE \"a\\tb\"", NULL, &r);
    CHECK_STR(lf_only(r.out), "a\\tb\n");
    run_result_free(&r);

    run_command("echo", "-ne \"a\\nb\"", NULL, &r);
    CHECK_STR(lf_only(r.out), "a\nb");
    run_result_free(&r);

    // not options: printed as they are
    run_command("echo", "-x -n -", NULL, &r);
    CHECK_STR(lf_only(r.out), "-x -n -\n");
    run_result_free(&r);

    run_command("echo", "--version", NULL, &r);
    CHECK(strstr(r.out, "echo") != NULL);
    run_result_free(&r);

    run_command("echo", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: echo") != NULL);
    run_result_free(&r);
}

static void test_arch(void) {
    RunResult r;
    const char* known[] = { "x86_64\n", "i686\n", "aarch64\n", "armv7l\n", "ia64\n", "mips\n",
                            "alpha\n", "powerpc\n", "sh4\n", "anycpu\n", "unknown\n" };
    size_t i;
    int matches = 0;

    printf("arch\n");
    run_command("arch", "", NULL, &r);
    lf_only(r.out);
    for (i = 0; i < sizeof known / sizeof known[0]; i++)
        if (strcmp(r.out, known[i]) == 0)
            matches++;
    CHECK_INT(matches, 1);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
}

static void test_pwd(void) {
    RunResult r;
    char expected[MAX_PATH * 2];

    printf("pwd\n");

    // the directory it runs in, spelled as a virtual path
    run_command("pwd", "-P", NULL, &r);
    snprintf(expected, sizeof expected, "%s\n", virtualWorkDir);
    CHECK_STR(lf_only(r.out), expected);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_command("pwd", "", NULL, &r);
    CHECK_STR(lf_only(r.out), expected);
    run_result_free(&r);

    run_command("pwd", "--bogus", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "unrecognized option '--bogus'") != NULL);
    run_result_free(&r);

    run_command("pwd", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: pwd") != NULL);
    run_result_free(&r);
}

int main(void) {
    tools_init("basic_test");

    test_echo();
    test_arch();
    test_pwd();
    return test_summary("basic_test");
}
