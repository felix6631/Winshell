/*
 * Tests of the shell: cd and the virtual file system, and wildcards.
 */

#include "testtools.h"

static void test_cd(void) {
    RunResult r;
    char expected[MAX_PATH * 4], line[MAX_PATH * 3], parent[MAX_PATH * 2];
    char* last;

    printf("cd and the virtual file system\n");

    make_directory("sub");
    make_directory("sub\\deeper");
    write_text(workDir, "plain.txt", "x");
    write_text(workDir, "in.txt", "one\n");

    // the parent of the work directory, worked out here
    strcpy(parent, virtualWorkDir);
    last = strrchr(parent, '/');
    if (last != NULL)
        *last = '\0';

    // the shell starts in the directory it was given
    snprintf(expected, sizeof expected, "%s\n", virtualWorkDir);
    SCRIPT_OUT("pwd\n", expected);

    // the root and the drive
    SCRIPT_OUT("cd /\npwd\n", "/\n");
    SCRIPT_OUT("cd /\ncd ..\npwd\n", "/\n");
    SCRIPT_OUT("cd /c\npwd\n", "/c\n");

    // an absolute path, and one level up from it
    snprintf(line, sizeof line, "cd %s\npwd\ncd ..\npwd\n", virtualWorkDir);
    snprintf(expected, sizeof expected, "%s\n%s\n", virtualWorkDir, parent);
    SCRIPT_OUT(line, expected);

    // walking up from the work directory reaches the root and stops there
    {
        char climb[MAX_PATH];
        int i;
        climb[0] = '\0';
        for (i = 0; i < 30; i++)
            strcat(climb, "cd ..\n");
        strcat(climb, "pwd\n");
        SCRIPT_OUT(climb, "/\n");
    }

    // the drive root, in every spelling; a case that read past a string once
    SCRIPT_OUT("cd /c/\npwd\ncd /C\npwd\ncd C:\\\npwd\ncd C:\npwd\n", "/c\n/c\n/c\n/c\n");

    // from one level below the drive root up to it
    SCRIPT_OUT("cd /c/Windows\ncd ..\npwd\n", "/c\n");

    // relative paths, "." and ".."
    snprintf(expected, sizeof expected, "%s/sub\n%s/sub/deeper\n%s\n%s/sub\n",
             virtualWorkDir, virtualWorkDir, virtualWorkDir, virtualWorkDir);
    SCRIPT_OUT("cd sub\npwd\ncd deeper\npwd\ncd ../..\npwd\ncd ./sub/./deeper/..\npwd\n", expected);

    // Windows notation works as well
    snprintf(line, sizeof line, "cd \"%s\\sub\"\npwd\n", workDir);
    snprintf(expected, sizeof expected, "%s/sub\n", virtualWorkDir);
    SCRIPT_OUT(line, expected);

    // cd -
    snprintf(expected, sizeof expected, "%s/sub\n%s\n%s\n", virtualWorkDir, virtualWorkDir, virtualWorkDir);
    SCRIPT_OUT("cd sub\npwd\ncd -\npwd\n", expected);

    // errors leave the directory where it was
    script("cd nosuchdir\npwd\n", &r);
    snprintf(expected, sizeof expected, "%s\n", virtualWorkDir);
    CHECK_STR(r.out, expected);
    CHECK(strstr(r.err, "cd: nosuchdir: No such file or directory") != NULL);
    run_result_free(&r);

    script("cd plain.txt\n", &r);
    CHECK(strstr(r.err, "cd: plain.txt: Not a directory") != NULL);
    run_result_free(&r);

    script("cd /nodrive\n", &r);
    CHECK(strstr(r.err, "cd: /nodrive: No such file or directory") != NULL);
    run_result_free(&r);

    script("cd a b\n", &r);
    CHECK(strstr(r.err, "cd: too many arguments") != NULL);
    run_result_free(&r);

    // in the root, relative names know the drives only
    script("cd /\ncat in.txt\ncat /\n", &r);
    CHECK(strstr(r.err, "cat: in.txt: No such file or directory") != NULL);
    CHECK(strstr(r.err, "cat: /: Is a directory") != NULL);
    run_result_free(&r);

    // the programs started by the shell see the virtual directory too
    SCRIPT_OUT("cd /\npwd\ncd c\npwd\n", "/\n/c\n");

    // cd in front of a pipe runs in a subshell: nothing changes
    script("cd / | cat\npwd\n", &r);
    CHECK_STR(r.err, "");
    snprintf(expected, sizeof expected, "%s\n", virtualWorkDir);
    CHECK_STR(r.out, expected);
    run_result_free(&r);
}

static void test_glob(void) {
    RunResult r;
    char line[MAX_PATH * 3], expected[MAX_PATH * 4];
    char dir[MAX_PATH * 2];

    printf("wildcards\n");

    snprintf(dir, sizeof dir, "%s\\globdir", workDir);
    CreateDirectoryA(dir, NULL);
    snprintf(line, sizeof line, "%s\\globdir\\sub", workDir);
    CreateDirectoryA(line, NULL);
    snprintf(line, sizeof line, "%s\\globdir\\other", workDir);
    CreateDirectoryA(line, NULL);
    write_text(dir, "a.txt", "1");
    write_text(dir, "b.txt", "2");
    write_text(dir, "c.dat", "3");
    write_text(dir, ".hidden.txt", "4");
    write_text(dir, "Z.TXT", "5");
    snprintf(line, sizeof line, "%s\\sub", dir);
    write_text(line, "x.txt", "6");
    write_text(line, "y.dat", "7");
    snprintf(line, sizeof line, "%s\\other", dir);
    write_text(line, "x.log", "8");

    write_text(dir, "with space.txt", "9");

    // every script starts in the directory with the fixtures
    {
        char text[2048];
        #define GLOB_OUT(commands, expectedText) \
            do { \
                snprintf(text, sizeof text, "cd globdir\n%s", commands); \
                SCRIPT_OUT(text, expectedText); \
            } while (0)

        // the basics: *, ?, [..], ranges and negation
        GLOB_OUT("echo *.txt\n", "a.txt b.txt with space.txt Z.TXT\n");
        GLOB_OUT("echo ?.txt\n", "a.txt b.txt Z.TXT\n");
        GLOB_OUT("echo [ab].txt\n", "a.txt b.txt\n");
        GLOB_OUT("echo [a-b].*\n", "a.txt b.txt\n");
        GLOB_OUT("echo [!a].txt\n", "b.txt Z.TXT\n");
        GLOB_OUT("echo [^ab].*\n", "c.dat Z.TXT\n");
        GLOB_OUT("echo a.*\n", "a.txt\n");
        GLOB_OUT("echo *\n", "a.txt b.txt c.dat other sub with space.txt Z.TXT\n");

        // names are matched without regard to case
        GLOB_OUT("echo *.TXT\n", "a.txt b.txt with space.txt Z.TXT\n");
        GLOB_OUT("echo z*\n", "Z.TXT\n");

        // dot names only by a pattern that starts with a dot
        GLOB_OUT("echo .*\n", ".hidden.txt\n");
        GLOB_OUT("echo .h*\n", ".hidden.txt\n");

        // quoting takes the wildcards away
        GLOB_OUT("echo \"*.txt\"\n", "*.txt\n");
        GLOB_OUT("echo '*.txt' \"?\"\n", "*.txt ?\n");
        GLOB_OUT("echo a\"*\"\n", "a*\n");
        GLOB_OUT("echo \"a\"*\n", "a.txt\n");

        // no match: the word is left as it was typed; an unclosed [ is no wildcard
        GLOB_OUT("echo *.zzz\n", "*.zzz\n");
        GLOB_OUT("echo x[ [ a[b\n", "x[ [ a[b\n");

        // in subdirectories, with the path as typed
        GLOB_OUT("echo sub/*\n", "sub/x.txt sub/y.dat\n");
        GLOB_OUT("echo sub\\*.dat\n", "sub\\y.dat\n");
        GLOB_OUT("echo */*.txt\n", "sub/x.txt\n");
        GLOB_OUT("echo s*/x.*\n", "sub/x.txt\n");
        GLOB_OUT("echo */\n", "other/ sub/\n");
        GLOB_OUT("echo ./*.dat ../globdir/c.*\n", "./c.dat ../globdir/c.dat\n");
        GLOB_OUT("echo sub/nothing*\n", "sub/nothing*\n");

        // a pattern is one word, a match is a word each: spaces in names survive
        GLOB_OUT("echo w*\n", "with space.txt\n");
        GLOB_OUT("cat w* | cat -n\n", "     1\t9");     // the file has no line feed at its end
        GLOB_OUT("ls w*\n", "with space.txt\n");

        // the programs get the names, so ls and cat work with them
        GLOB_OUT("ls *.dat\n", "c.dat\n");
        GLOB_OUT("cat sub/*.txt\n", "6");
        GLOB_OUT("ls sub/*\n", "sub/x.txt\nsub/y.dat\n");

        // wildcards in the file name of a redirection are not expanded
        script("cd globdir\necho hi > *.out\n", &r);
        CHECK(strstr(r.err, "Cannot open '*.out' for writing") != NULL);
        run_result_free(&r);

        #undef GLOB_OUT
    }

    // virtual paths: absolute, and through the root
    snprintf(line, sizeof line, "echo %s/globdir/*.dat\n", virtualWorkDir);
    snprintf(expected, sizeof expected, "%s/globdir/c.dat\n", virtualWorkDir);
    SCRIPT_OUT(line, expected);

    {
        char drive = virtualWorkDir[1];
        snprintf(line, sizeof line, "echo /%c*\n", drive);
        snprintf(expected, sizeof expected, "/%c\n", drive);
        SCRIPT_OUT(line, expected);
    }

    // Windows notation as well
    snprintf(line, sizeof line, "echo \"%s\\globdir\"\\*.dat\n", workDir);
    {
        char* backslashed = _strdup(workDir);
        snprintf(expected, sizeof expected, "%s\\globdir\\c.dat\n", backslashed);
        free(backslashed);
    }
    SCRIPT_OUT(line, expected);
}

static void test_pwd_variables(void) {
    char expected[MAX_PATH * 4];

    printf("PWD and OLDPWD\n");
    make_directory("sub");

    // PWD is exported: the programs the shell starts find the directory by it
    snprintf(expected, sizeof expected, "%s/sub\n", virtualWorkDir);
    SCRIPT_OUT("cd sub\nprintenv PWD\n", expected);
    SCRIPT_OUT("cd /\nprintenv PWD\n", "/\n");

    // OLDPWD is the directory before the last change; cd - goes back and says where
    snprintf(expected, sizeof expected, "%s/sub\n", virtualWorkDir);
    SCRIPT_OUT("cd sub\ncd ..\nprintenv OLDPWD\n", expected);
    snprintf(expected, sizeof expected, "%s\n%s\n", virtualWorkDir, virtualWorkDir);
    SCRIPT_OUT("cd sub\ncd -\nprintenv PWD\n", expected);

    // a cd that fails changes neither
    snprintf(expected, sizeof expected, "%s\n", virtualWorkDir);
    SCRIPT_OUT("cd nosuch\nprintenv PWD\n", expected);
    SCRIPT_OUT("cd nosuch\nprintenv OLDPWD\n", "");

    // an assignment cannot make the shell believe it is somewhere else
    snprintf(expected, sizeof expected, "%s\n", virtualWorkDir);
    SCRIPT_OUT("PWD=/bogus\npwd\n", expected);
}

int main(void) {
    tools_init("shell_cd_test");

    test_cd();
    test_pwd_variables();
    test_glob();
    return test_summary("shell_cd_test");
}
