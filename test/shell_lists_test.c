/*
 * Tests of the shell: lists of commands (; && ||), exit status, variables, exit.
 */

#include "testtools.h"

/** ;  &&  ||  and the status of commands. */
static void test_lists(void) {
    RunResult r;

    printf("lists of commands\n");

    SCRIPT_OUT("echo a; echo b\n", "a\nb\n");
    SCRIPT_OUT("echo a;echo b;echo c\n", "a\nb\nc\n");
    SCRIPT_OUT("echo a ;\n", "a\n");

    // && and ||: the status of what ran last decides
    SCRIPT_OUT("echo a && echo b\n", "a\nb\n");
    SCRIPT_OUT("echo a || echo b\n", "a\n");
    SCRIPT_OUT("cat nosuch.txt 2> errors.txt && echo no\n", "");
    SCRIPT_OUT("echo x > ok.txt && cat nosuch.txt && echo no || echo yes\n", "yes\n");
    SCRIPT_OUT("echo ok && echo first || echo second\n", "ok\nfirst\n");
    SCRIPT_OUT("cat nosuch.txt || echo fallback\n", "fallback\n");
    SCRIPT_OUT("cat nosuch.txt; echo $?\n", "1\n");
    SCRIPT_OUT("echo hi; echo $?\n", "hi\n0\n");

    // a failing program that is not found, and builtins
    script("nosuchprogram || echo not found $?\n", &r);
    CHECK_STR(r.out, "not found 127\n");
    run_result_free(&r);
    SCRIPT_OUT("cd /nodrive || echo failed\n", "failed\n");
    SCRIPT_OUT("cd / && pwd\n", "/\n");

    // ... and what is skipped keeps the status of the last command that ran
    SCRIPT_OUT("cat nosuch.txt && echo a && echo b; echo $?\n", "1\n");
    SCRIPT_OUT("echo a || echo b || echo c; echo $?\n", "a\n0\n");

    // the second command sees the directory the first one went to
    SCRIPT_OUT("cd nodirhere && pwd\n", "");                  // the cd fails: pwd does not run
    SCRIPT_OUT("cd / ; pwd ; cd c && pwd\n", "/\n/c\n");

    // quoted operators are text, a pipe is part of its command
    SCRIPT_OUT("echo \"a;b\" 'c&&d' \"e||f\"\n", "a;b c&&d e||f\n");
    SCRIPT_OUT("echo b a | sort && echo done\n", "b a\ndone\n");

    // syntax errors: nothing of the line runs
    script("echo a ;; echo b\necho next\n", &r);
    CHECK(strstr(r.err, "Syntax error near unexpected token ';'") != NULL);
    CHECK_STR(r.out, "next\n");
    run_result_free(&r);
    script("echo a &&\n", &r);
    CHECK(strstr(r.err, "missing command after '&&'") != NULL);
    CHECK_STR(r.out, "");
    run_result_free(&r);
    script("echo a & echo b\n", &r);
    CHECK(strstr(r.err, "'&' is not supported") != NULL);
    CHECK_STR(r.out, "");
    run_result_free(&r);
    script("&& echo a\n", &r);
    CHECK(strstr(r.err, "near unexpected token '&&'") != NULL);
    run_result_free(&r);

    // a syntax error is status 2
    SCRIPT_OUT("echo a ;; echo b\necho $?\n", "2\n");
}

/** $NAME, ${NAME}, $?, ~, export, unset and exit. */
static void test_variables(void) {
    RunResult r;
    char expected[MAX_PATH * 3], profile[MAX_PATH], home[MAX_PATH];
    const char* envHome;

    printf("variables\n");

    // set in the environment of the shell
    _putenv_s("WS_TEST", "hello world");
    _putenv_s("WS_SPACES", "a   b");
    _putenv_s("WS_GLOBBY", "*.txt");

    SCRIPT_OUT("echo $WS_TEST\n", "hello world\n");
    SCRIPT_OUT("echo \"$WS_SPACES\"\n", "a   b\n");           // one word: the blanks stay
    SCRIPT_OUT("echo $WS_SPACES\n", "a b\n");                 // cut into words, joined by echo
    SCRIPT_OUT("echo '$WS_TEST'\n", "$WS_TEST\n");
    SCRIPT_OUT("echo ${WS_TEST}x\n", "hello worldx\n");
    SCRIPT_OUT("echo [$WS_NOT_SET]\n", "[]\n");
    SCRIPT_OUT("echo $WS_NOT_SET x\n", "x\n");                // not even an empty word
    SCRIPT_OUT("echo \"$WS_NOT_SET\" x\n", " x\n");           // an empty word

    // names are not case sensitive, as Windows names are not
    SCRIPT_OUT("echo $ws_test\n", "hello world\n");

    // a variable can hold a wildcard, which then acts as one, unless it is quoted
    {
        char dir[MAX_PATH * 2];
        snprintf(dir, sizeof dir, "%s\\vardir", workDir);
        CreateDirectoryA(dir, NULL);
        write_text(dir, "one.txt", "1");
        write_text(dir, "two.txt", "2");
        SCRIPT_OUT("cd vardir\necho $WS_GLOBBY\n", "one.txt two.txt\n");
        SCRIPT_OUT("cd vardir\necho \"$WS_GLOBBY\"\n", "*.txt\n");
    }

    // PWD follows cd
    snprintf(expected, sizeof expected, "%s/vardir\n", virtualWorkDir);
    SCRIPT_OUT("cd vardir && echo $PWD\n", expected);
    snprintf(expected, sizeof expected, "%s\n/\n", virtualWorkDir);
    SCRIPT_OUT("echo $PWD; cd /; echo $PWD\n", expected);
    snprintf(expected, sizeof expected, "%s\n", virtualWorkDir);
    SCRIPT_OUT("cd vardir; cd -\n", expected);                 // cd - shows where it went
    snprintf(expected, sizeof expected, "%s/vardir\n", virtualWorkDir);
    SCRIPT_OUT("cd vardir; cd ..; echo $OLDPWD\n", expected);

    // the home directory: the variable when it is set, else the Windows profile as a virtual path
    envHome = getenv("HOME");
    if (envHome != NULL) {
        snprintf(home, sizeof home, "%s", envHome);
    } else {
        const char* userProfile = getenv("USERPROFILE");
        snprintf(profile, sizeof profile, "%s", userProfile != NULL ? userProfile : "");
        expected_virtual(profile, home, sizeof home);
    }
    snprintf(expected, sizeof expected, "%s\n", home);
    SCRIPT_OUT("echo $HOME\n", expected);
    SCRIPT_OUT("echo ~\n", expected);
    snprintf(expected, sizeof expected, "%s/Documents\n", home);
    SCRIPT_OUT("echo ~/Documents\n", expected);
    SCRIPT_OUT("echo \"~\" a~\n", "~ a~\n");

    // export and unset
    SCRIPT_OUT("export WS_NEW=value; echo $WS_NEW\n", "value\n");
    SCRIPT_OUT("export WS_NEW=one WS_OTHER=two; echo $WS_NEW $WS_OTHER\n", "one two\n");
    SCRIPT_OUT("export WS_NEW=value; unset WS_NEW; echo [$WS_NEW]\n", "[]\n");
    SCRIPT_OUT("export WS_EMPTY=; echo [$WS_EMPTY]\n", "[]\n");

    // programs started by the shell get the variables: pwd uses PWD, which export cannot fake
    script("export 9bad=1\n", &r);
    CHECK(strstr(r.err, "export: '9bad=1': not a valid identifier") != NULL);
    run_result_free(&r);

    // exit
    {
        RunResult result;
        run_program(shellPath, "", "exit 3\n", workDir, &result);
        CHECK_INT(result.exitCode, 3);
        run_result_free(&result);
        run_program(shellPath, "", "echo before; exit; echo after\n", workDir, &result);
        CHECK_INT(result.exitCode, 0);
        CHECK(strstr(result.out, "before") != NULL && strstr(result.out, "after") == NULL);
        run_result_free(&result);
        run_program(shellPath, "", "cat nosuch.txt\nexit\n", workDir, &result);
        CHECK_INT(result.exitCode, 1);                     // the status of the last command
        run_result_free(&result);
        run_program(shellPath, "", "quit\n", workDir, &result);
        CHECK_INT(result.exitCode, 0);
        run_result_free(&result);
    }
}

/** NAME=value alone, in front of a command, and what export makes of them. */
static void test_assignments(void) {
    RunResult r;
    char expected[MAX_PATH * 3], home[MAX_PATH];
    const char* userProfile;

    printf("assignments\n");

    // a shell started by the script shows which variables a program gets
    write_text(workDir, "probe_a.txt", "echo \"[$WS_A]\"\n");
    write_text(workDir, "probe_b.txt", "echo \"[$WS_B]\"\n");

    SCRIPT_OUT("WS_V=1; echo $WS_V\n", "1\n");
    SCRIPT_OUT("WS_V=1 WS_W=2; echo $WS_V $WS_W\n", "1 2\n");
    SCRIPT_OUT("WS_V=one; WS_V=two; echo $WS_V\n", "two\n");
    SCRIPT_OUT("WS_V=a=b; echo $WS_V\n", "a=b\n");
    SCRIPT_OUT("WS_V=; echo [$WS_V]\n", "[]\n");
    SCRIPT_OUT("WS_V=1; echo $?\n", "0\n");
    SCRIPT_OUT("WS_V=1 && echo ok\n", "ok\n");
    SCRIPT_OUT("WS_V=1; WS_U=$WS_V$WS_V; echo $WS_U\n", "11\n");

    // quotes are removed and keep the value in one piece
    SCRIPT_OUT("WS_V=\"x   y\"; echo \"$WS_V\"\n", "x   y\n");
    SCRIPT_OUT("WS_V='$a b'; echo \"$WS_V\"\n", "$a b\n");
    SCRIPT_OUT("WS_V=a\"b c\"d; echo \"$WS_V\"\n", "ab cd\n");

    // no word splitting and no wildcards in the value, also when it comes from a variable
    _putenv_s("WS_SP", "a   b");
    SCRIPT_OUT("WS_V=$WS_SP; echo \"$WS_V\"\n", "a   b\n");
    SCRIPT_OUT("WS_V=*.txt; echo \"$WS_V\"\n", "*.txt\n");

    // ~ at the start of the value is the home directory
    if (getenv("HOME") != NULL) {
        snprintf(home, sizeof home, "%s", getenv("HOME"));
    } else {
        userProfile = getenv("USERPROFILE");
        expected_virtual(userProfile != NULL ? userProfile : "", home, sizeof home);
    }
    snprintf(expected, sizeof expected, "%s/x\n", home);
    SCRIPT_OUT("WS_V=~/x; echo \"$WS_V\"\n", expected);

    // it is the command position that makes an assignment: anywhere else it is an argument
    SCRIPT_OUT("echo WS_V=1\n", "WS_V=1\n");
    SCRIPT_OUT("echo a WS_V=1 b\n", "a WS_V=1 b\n");
    SCRIPT_OUT("echo \"WS_V\"=1\n", "WS_V=1\n");
    script("\"WS_V\"=1\n", &r);
    CHECK(strstr(r.err, "Failed to execute command") != NULL);
    run_result_free(&r);
    script("$WS_NOT_SET=1\n", &r);
    CHECK(strstr(r.err, "Failed to execute command") != NULL);
    run_result_free(&r);

    // the variable is the shell's only: the programs do not get it, until it is exported
    SCRIPT_OUT("WS_A=1; shell < probe_a.txt\n", "[]\n");
    SCRIPT_OUT("WS_A=1; export WS_A; shell < probe_a.txt\n", "[1]\n");
    SCRIPT_OUT("export WS_A; WS_A=2; shell < probe_a.txt\n", "[2]\n");
    SCRIPT_OUT("WS_A=3; export WS_A=4; shell < probe_a.txt\n", "[4]\n");
    SCRIPT_OUT("export WS_A=5; WS_A=6; shell < probe_a.txt\n", "[6]\n");     // stays exported
    SCRIPT_OUT("export WS_A=5; unset WS_A; shell < probe_a.txt\n", "[]\n");
    SCRIPT_OUT("WS_A=1; unset WS_A; echo [$WS_A]\n", "[]\n");

    // in front of a command: the environment of that command only
    SCRIPT_OUT("WS_B=hi shell < probe_b.txt\n", "[hi]\n");
    SCRIPT_OUT("WS_B=hi shell < probe_b.txt; echo [$WS_B]\n", "[hi]\n[]\n");
    SCRIPT_OUT("WS_B=set; WS_B=other shell < probe_b.txt; echo $WS_B\n", "[other]\nset\n");
    SCRIPT_OUT("export WS_B=kept; WS_B=temp shell < probe_b.txt; shell < probe_b.txt\n", "[temp]\n[kept]\n");
    SCRIPT_OUT("WS_B=1 WS_B=2 shell < probe_b.txt\n", "[2]\n");
    SCRIPT_OUT("WS_B=hi echo [$WS_B]\n", "[]\n");                // expanded before it is set
    SCRIPT_OUT("WS_B=hi echo done\n", "done\n");
    // a command of a pipeline gets its own
    SCRIPT_OUT("WS_B=left shell < probe_b.txt | cat\n", "[left]\n");

    // its status and its redirections
    SCRIPT_OUT("WS_B=1 cat nosuch.txt 2> assign_err.txt; echo $?\n", "1\n");
    script("WS_V=1 > assign_made.txt\n", &r);
    CHECK_STR(r.out, "");
    run_result_free(&r);
    CHECK(path_exists_in(workDir, "assign_made.txt"));
    SCRIPT_OUT("WS_V=kept; WS_V=gone | cat; echo $WS_V\n", "kept\n");         // in a pipeline: not the shell's
    SCRIPT_OUT("WS_V=1 > /c/nodrive_here/x.txt; echo $?\n", "1\n");
}

int main(void) {
    tools_init("shell_lists_test");

    test_lists();
    test_variables();
    test_assignments();
    return test_summary("shell_lists_test");
}
