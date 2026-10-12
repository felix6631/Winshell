/*
 * Tests of printenv and env, run as the programs they are.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void test_printenv(void) {
    RunResult r;

    printf("printenv\n");

    _putenv_s("WS_PE", "val");
    SetEnvironmentVariableA("WS_EMPTY", "");

    run_command("printenv", "WS_PE", NULL, &r);
    CHECK_STR(r.out, "val\n");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    // names are not case sensitive on Windows
    run_command("printenv", "ws_pe", NULL, &r);
    CHECK_STR(r.out, "val\n");
    run_result_free(&r);

    // several names; a missing one only changes the status
    run_command("printenv", "WS_PE WS_NOPE WS_PE", NULL, &r);
    CHECK_STR(r.out, "val\nval\n");
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    run_command("printenv", "WS_NOPE", NULL, &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    // a name has no '='
    run_command("printenv", "WS_PE=val", NULL, &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    // an empty value is a value
    run_command("printenv", "WS_EMPTY", NULL, &r);
    CHECK_STR(r.out, "\n");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    // -0: NUL ends the line
    run_command("printenv", "-0 WS_PE", NULL, &r);
    CHECK_INT((int)r.outLength, 4);
    CHECK(r.outLength == 4 && memcmp(r.out, "val\0", 4) == 0);
    run_result_free(&r);

    // all: NAME=value lines, and none of the per-drive "=C:" entries of Windows
    run_command("printenv", "", NULL, &r);
    CHECK(has_line(r.out, "WS_PE=val"));
    CHECK(strstr(r.out, "\n=") == NULL && r.out[0] != '=');
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_command("printenv", "-x", NULL, &r);
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);

    run_command("printenv", "--version", NULL, &r);
    CHECK(strstr(r.out, "printenv") != NULL);
    run_result_free(&r);
}

static void test_env_print(void) {
    RunResult r;

    printf("env: environment\n");

    _putenv_s("WS_PE", "val");

    run_command("env", "", NULL, &r);
    CHECK(has_line(r.out, "WS_PE=val"));
    CHECK(r.out[0] != '=' && strstr(r.out, "\n=") == NULL);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_command("env", "WS_X=1", NULL, &r);
    CHECK(has_line(r.out, "WS_X=1") && has_line(r.out, "WS_PE=val"));
    run_result_free(&r);

    // a variable that is set again is replaced, not repeated
    run_command("env", "WS_PE=new", NULL, &r);
    CHECK(has_line(r.out, "WS_PE=new") && !has_line(r.out, "WS_PE=val"));
    run_result_free(&r);
    run_command("env", "ws_pe=new", NULL, &r);
    CHECK(has_line(r.out, "ws_pe=new") && !has_line(r.out, "WS_PE=val"));
    run_result_free(&r);

    // -i and -u
    run_command("env", "-i WS_X=1", NULL, &r);
    CHECK_STR(r.out, "WS_X=1\n");
    run_result_free(&r);
    run_command("env", "-i", NULL, &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    run_command("env", "--ignore-environment WS_A=1 WS_B=2", NULL, &r);
    CHECK_STR(r.out, "WS_A=1\nWS_B=2\n");
    run_result_free(&r);
    run_command("env", "-u WS_PE", NULL, &r);
    CHECK(!has_line(r.out, "WS_PE=val") && strstr(r.out, "WS_PE") == NULL);
    run_result_free(&r);
    run_command("env", "-uWS_PE", NULL, &r);
    CHECK(strstr(r.out, "WS_PE") == NULL);
    run_result_free(&r);
    run_command("env", "--unset=WS_PE", NULL, &r);
    CHECK(strstr(r.out, "WS_PE") == NULL);
    run_result_free(&r);
    run_command("env", "-i -u WS_PE WS_Y=2", NULL, &r);
    CHECK_STR(r.out, "WS_Y=2\n");
    run_result_free(&r);
    run_command("env", "-iu WS_PE WS_Y=2", NULL, &r);          // grouped
    CHECK_STR(r.out, "WS_Y=2\n");
    run_result_free(&r);

    // -0
    run_command("env", "-0 -i WS_A=1", NULL, &r);
    CHECK_INT((int)r.outLength, 7);
    CHECK(r.outLength == 7 && memcmp(r.out, "WS_A=1\0", 7) == 0);
    run_result_free(&r);
    run_command("env", "-0 -i WS_A=1 printenv", NULL, &r);
    CHECK_INT(r.exitCode, 125);
    run_result_free(&r);

    // bad options
    run_command("env", "-z", NULL, &r);
    CHECK_INT(r.exitCode, 125);
    run_result_free(&r);
    run_command("env", "-u", NULL, &r);
    CHECK_INT(r.exitCode, 125);
    run_result_free(&r);
    run_command("env", "-u A=B", NULL, &r);
    CHECK_INT(r.exitCode, 125);
    run_result_free(&r);

    run_command("env", "--version", NULL, &r);
    CHECK(strstr(r.out, "env") != NULL);
    run_result_free(&r);
}

static void test_env_run(void) {
    RunResult r;
    char arguments[MAX_PATH * 3], expected[MAX_PATH * 2];
    static char longArguments[32768];

    printf("env: running a command\n");

    _putenv_s("WS_PE", "val");

    // the programs of the bin directory, as the shell finds them
    run_command("env", "echo hi there", NULL, &r);
    CHECK_STR(lf_only(r.out), "hi there\n");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_command("env", "echo.exe x", NULL, &r);
    CHECK_STR(lf_only(r.out), "x\n");
    run_result_free(&r);

    // arguments come through as they are
    run_command("env", "echo \"a  b\" c", NULL, &r);
    CHECK_STR(lf_only(r.out), "a  b c\n");
    run_result_free(&r);

    // the command gets the changed environment
    run_command("env", "WS_CHILD=hello printenv WS_CHILD", NULL, &r);
    CHECK_STR(r.out, "hello\n");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    run_command("env", "WS_PE=other printenv WS_PE", NULL, &r);
    CHECK_STR(r.out, "other\n");
    run_result_free(&r);
    run_command("env", "-u WS_PE printenv WS_PE", NULL, &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 1);              // the status of the command
    run_result_free(&r);
    // (the sanitizer build of a program needs PATH to find its runtime library, so it is kept)
    snprintf(longArguments, sizeof longArguments, "-i \"PATH=%s;\" WS_A=1 printenv WS_A", getenv("PATH") != NULL ? getenv("PATH") : "");
    run_command("env", longArguments, NULL, &r);
    CHECK_STR(r.out, "1\n");
    run_result_free(&r);

    // the input and the output are the ones of env
    run_command("env", "sort", "b\na\n", &r);
    CHECK_STR(lf_only(r.out), "a\nb\n");
    run_result_free(&r);
    snprintf(longArguments, sizeof longArguments, "-i \"PATH=%s;\" printenv NOPE", getenv("PATH") != NULL ? getenv("PATH") : "");
    run_command("env", longArguments, NULL, &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    // options end at the command: what follows belongs to it
    run_command("env", "echo -i -u x", NULL, &r);
    CHECK_STR(lf_only(r.out), "-i -u x\n");
    run_result_free(&r);
    run_command("env", "-- echo y", NULL, &r);
    CHECK_STR(lf_only(r.out), "y\n");
    run_result_free(&r);

    // not found, and not a program
    run_command("env", "nosuchprogram", NULL, &r);
    CHECK_INT(r.exitCode, 127);
    CHECK(strstr(r.err, "nosuchprogram") != NULL);
    run_result_free(&r);

    // -C: the command runs in another directory, which PWD describes as well
    make_directory("envsub");
    snprintf(arguments, sizeof arguments, "-C %s/envsub pwd", virtualWorkDir);
    snprintf(expected, sizeof expected, "%s/envsub\n", virtualWorkDir);
    run_command("env", arguments, NULL, &r);
    CHECK_STR(lf_only(r.out), expected);
    run_result_free(&r);
    snprintf(arguments, sizeof arguments, "--chdir=%s/envsub pwd -P", virtualWorkDir);
    run_command("env", arguments, NULL, &r);
    CHECK_STR(lf_only(r.out), expected);
    run_result_free(&r);
    run_command("env", "-C nosuchdirhere pwd", NULL, &r);
    CHECK_INT(r.exitCode, 125);
    run_result_free(&r);
    run_command("env", "-C envsub", NULL, &r);          // no command
    CHECK_INT(r.exitCode, 125);
    run_result_free(&r);
}

int main(void) {
    tools_init("env_test");

    test_printenv();
    test_env_print();
    test_env_run();
    return test_summary("env_test");
}
