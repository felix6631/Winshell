/*
 * Tests of the shell with the commands of the project: files, text tools and
 * filters together with pipes, redirection and wildcards.
 */

#include "testtools.h"

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
    // input from a pipe has no size: the numbers are seven wide; from a file they are as wide as it is
    SCRIPT_OUT("cd textcmd\ncat numbers.txt | wc\n", "     20      20      71\n");
    SCRIPT_OUT("cd textcmd\nwc < numbers.txt\n", "20 20 71\n");
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

/** sort, uniq, cut and tr in pipelines: what they are for. */
static void test_filter_pipelines(void) {
    char dir[MAX_PATH * 2];

    printf("sort, uniq, cut and tr in pipelines\n");

    snprintf(dir, sizeof dir, "%s\\filters", workDir);
    CreateDirectoryA(dir, NULL);
    write_text(dir, "fruit.txt", "pear\napple\npear\nfig\napple\npear\n");
    write_text(dir, "people.csv", "bob,30,sales\nalice,25,dev\ncarol,35,dev\n");
    write_text(dir, "crlf.csv", "bob,30\r\nalice,25\r\n");

    // the classic: how often does each word occur, most frequent first
    SCRIPT_OUT("cd filters\nsort fruit.txt | uniq -c | sort -rn | head -n 1\n", "      3 pear\n");
    SCRIPT_OUT("cd filters\nsort -u fruit.txt\n", "apple\nfig\npear\n");
    SCRIPT_OUT("cd filters\ncat fruit.txt | sort | uniq -d\n", "apple\npear\n");

    // fields, sorted and counted
    SCRIPT_OUT("cd filters\ncut -d, -f3 people.csv | sort | uniq -c\n", "      2 dev\n      1 sales\n");
    SCRIPT_OUT("cd filters\nsort -t, -k2 -n people.csv | cut -d, -f1\n", "alice\nbob\ncarol\n");

    // tr in a pipeline, and with a file as input
    SCRIPT_OUT("cd filters\ncat fruit.txt | tr a-z A-Z | head -n 2\n", "PEAR\nAPPLE\n");
    SCRIPT_OUT("cd filters\ntr a-z A-Z < fruit.txt | sort -u | head -n 1\n", "APPLE\n");
    SCRIPT_OUT("cd filters\ncut -d, -f1 people.csv | tr \"\\n\" \" \"\n", "bob alice carol ");

    // a CR LF file goes through the same tools and keeps its line endings
    SCRIPT_OUT("cd filters\nsort crlf.csv | cut -d, -f2 | wc -l\n", "2\n");
    SCRIPT_OUT("cd filters\nsort crlf.csv | tr -d \"\\r\" | cat -E\n", "alice,25$\nbob,30$\n");
}

int main(void) {
    tools_init("shell_pipeline_test");

    test_file_commands();
    test_text_pipelines();
    test_filter_pipelines();
    return test_summary("shell_pipeline_test");
}
