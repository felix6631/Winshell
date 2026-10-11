/*
 * Tests of the shell, run as the program it is: a script goes in through
 * standard input and the output is checked. This covers running commands,
 * quoting, pipes, redirection and the cd builtin together with the
 * virtual file system.
 * Needs the programs in <repository>\bin (build with nmake first).
 *
 * Not covered here: Ctrl+C. It needs a real console to receive the event,
 * which a program with redirected handles does not have.
 */

#include "testlib.h"

static char workDir[MAX_PATH];
static char virtualWorkDir[MAX_PATH];
static char shellPath[MAX_PATH * 2];

/**
 * Runs the lines as a script. The shell prints its prompt before each line;
 * the prompts and CRs are removed from the output so that it can be compared.
 */
static void script(const char* lines, RunResult* r) {
    int status = run_program(shellPath, "", lines, workDir, r);
    CHECK(status == 0);
    CHECK(!r->timedOut);
    if (r->out != NULL)
        without_prompts(lf_only(r->out));
    if (r->err != NULL)
        lf_only(r->err);
}

#define SCRIPT_OUT(lines, expected) \
    do { RunResult r_; script(lines, &r_); CHECK_STR(r_.out, expected); run_result_free(&r_); } while (0)

static void make_directory(const char* relative) {
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\%s", workDir, relative);
    CreateDirectoryA(path, NULL);
}

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

    // cd inside a pipeline is not a builtin: nothing changes
    script("cd / | cat\npwd\n", &r);
    CHECK(strstr(r.err, "Failed to execute command 'cd'") != NULL);
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

/** rm, cp and mv together with wildcards, which is what the shell expands them for. */
static void test_file_commands(void) {
    RunResult r;
    char dir[MAX_PATH * 2], path[MAX_PATH * 3];

    printf("rm, cp and mv with wildcards\n");

    snprintf(dir, sizeof dir, "%s\\fscmd", workDir);
    // a clean directory: the shell itself removes what an earlier run left
    CreateDirectoryA(dir, NULL);
    script("cd fscmd\nrm -rf *\nrm -rf .?*\n", &r);
    run_result_free(&r);

    write_text(dir, "a.tmp", "1");
    write_text(dir, "b.tmp", "2");
    write_text(dir, "keep.txt", "3");
    write_text(dir, "one.txt", "4");
    write_text(dir, "two.txt", "5");
    write_text(dir, "x.log", "6");
    write_text(dir, "y.log", "7");
    snprintf(path, sizeof path, "%s\\dest", dir);
    CreateDirectoryA(path, NULL);
    snprintf(path, sizeof path, "%s\\logs", dir);
    CreateDirectoryA(path, NULL);

    SCRIPT_OUT("cd fscmd\nrm *.tmp\nls\n", "dest\nkeep.txt\nlogs\none.txt\ntwo.txt\nx.log\ny.log\n");
    SCRIPT_OUT("cd fscmd\ncp *.txt dest\nls dest\n", "keep.txt\none.txt\ntwo.txt\n");
    SCRIPT_OUT("cd fscmd\nmv *.log logs\nls logs\nls *.log\n", "x.log\ny.log\n");

    // a pattern without a match stays, and the tool says so
    script("cd fscmd\nrm *.nothing\n", &r);
    CHECK(strstr(r.err, "rm: cannot remove '*.nothing': No such file or directory") != NULL);
    run_result_free(&r);

    // recursion with a directory name given by a pattern
    SCRIPT_OUT("cd fscmd\ncp -r d* copy\nls copy\nrm -r copy\nls\n",
               "keep.txt\none.txt\ntwo.txt\n"             // the copy of dest
               "dest\nkeep.txt\nlogs\none.txt\ntwo.txt\n"); // after removing it
}

/** head, tail and wc in pipelines: the reason they read standard input. */
static void test_text_pipelines(void) {
    char dir[MAX_PATH * 2];
    char text[512];
    int i;

    printf("head, tail and wc in pipelines\n");

    snprintf(dir, sizeof dir, "%s\\textcmd", workDir);
    CreateDirectoryA(dir, NULL);
    text[0] = '\0';
    for (i = 1; i <= 20; i++) {
        char one[16];
        snprintf(one, sizeof one, "n%d\n", i);
        strcat(text, one);
    }
    write_text(dir, "numbers.txt", text);
    write_text(dir, "a.txt", "1");
    write_text(dir, "b.txt", "2");
    write_text(dir, "c.log", "3");
    {
        // what an earlier run left behind
        RunResult r;
        script("cd textcmd\nrm -f first.txt\n", &r);
        run_result_free(&r);
    }

    SCRIPT_OUT("cd textcmd\ncat numbers.txt | head -n 3\n", "n1\nn2\nn3\n");
    SCRIPT_OUT("cd textcmd\ncat numbers.txt | tail -n 2\n", "n19\nn20\n");
    SCRIPT_OUT("cd textcmd\ncat numbers.txt | tail -n +19 | head -n 1\n", "n19\n");
    SCRIPT_OUT("cd textcmd\ncat numbers.txt | wc -l\n", "20\n");
    SCRIPT_OUT("cd textcmd\ncat numbers.txt | head -n -17 | wc -l\n", "3\n");

    // how many files: ls into wc
    SCRIPT_OUT("cd textcmd\nls | wc -l\n", "4\n");
    SCRIPT_OUT("cd textcmd\nls *.txt | wc -l\n", "3\n");

    // the line count does not depend on the line endings of the input
    SCRIPT_OUT("cd textcmd\necho one | cat | wc -l\n", "1\n");

    // numbered, then the last of them
    SCRIPT_OUT("cd textcmd\ncat -n numbers.txt | tail -n 1\n", "    20\tn20\n");

    // redirection into and out of them
    SCRIPT_OUT("cd textcmd\nhead -n 2 numbers.txt > first.txt\nwc -l < first.txt\n", "2\n");
    // the files in the order the shell expands them; a.txt and b.txt have no line feed
    SCRIPT_OUT("cd textcmd\nhead -n 2 numbers.txt > first.txt\nwc -l *.txt\n",
               " 0 a.txt\n 0 b.txt\n 2 first.txt\n20 numbers.txt\n22 total\n");
}

int main(void) {
    make_work_dir("shell", workDir, sizeof workDir);
    expected_virtual(workDir, virtualWorkDir, sizeof virtualWorkDir);
    bin_path("shell", shellPath, sizeof shellPath);

    test_running();
    test_quoting();
    test_pipes();
    test_redirection();
    test_cd();
    test_glob();
    test_file_commands();
    test_text_pipelines();
    return test_summary("shell_test");
}
