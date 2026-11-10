/*
 * Tests of fmt, run as the program it is: filling paragraphs to a width and a goal, sentence ends and spacing, indentation,
 * -u -s -c -t, prefixes, tabs, several paragraphs and files, the options and the errors. GNU fmt is compared with by
 * gnu_compare.sh (more than a thousand combinations).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("fmt", arguments, input, expected, status);
}

static void message(const char* arguments, const char* part, int status) {
    RunResult r;

    run_tool(workDir, "fmt", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

int main(void) {
    tools_init("fmt_test");
    reset_dir(workDir);

    printf("filling\n");
    expect("-w 20", "one two three four five six seven eight nine ten\n", "one two three four\nfive six seven\neight nine ten\n", 0);
    expect("-w 20", "one\ntwo\nthree\n", "one two three\n", 0);                           // short lines are joined
    expect("-w 10", "alpha beta gamma delta\n", "alpha\nbeta\ngamma\ndelta\n", 0);
    expect("-20", "one two three four five six seven eight nine ten\n", "one two three four\nfive six seven\neight nine ten\n", 0);
    expect("-w 20 -g 12", "one two three four five six seven eight nine ten\n",
           "one two three four\nfive six seven\neight nine ten\n", 0);
    expect("-w 5", "supercalifragilistic word\n", "supercalifragilistic\nword\n", 0);       // a long word stands by itself
    expect("", "", "", 0);
    expect("", "no newline at the end", "no newline at the end\n", 0);

    printf("paragraphs, blank lines, indentation\n");
    expect("", "a b\n\nc d\n", "a b\n\nc d\n", 0);
    expect("", "a b\n\n\n\nc d\n", "a b\n\n\n\nc d\n", 0);                                 // blank lines are kept
    expect("", "a\n  b\n", "a\n  b\n", 0);                                              // another indent: another paragraph
    expect("", "  a b\n  c d\n", "  a b c d\n", 0);
    expect("-w 20", "   indented   words   keep   their   spacing   inside\n", "   indented   words\n   keep   their\n   spacing   inside\n", 0);
    expect("", "x   \n  \ny\n", "x\n\ny\n", 0);                                          // blanks on a blank line go

    printf("sentences and spacing\n");
    expect("", "End of sentence.  Next one.\n", "End of sentence.  Next one.\n", 0);
    expect("", "End of sentence. Next one.\n", "End of sentence. Next one.\n", 0);        // one space: not a sentence end
    expect("", "One.\nTwo.\n", "One.  Two.\n", 0);                                       // a period at the end of a line is
    expect("-u", "A   b.   C   d.\n", "A b.  C d.\n", 0);
    expect("-w 14", "First one. Second one. Third.\n", "First\none. Second\none. Third.\n", 0);

    printf("-s, -c, -t, -p\n");
    expect("-s -w 10", "aaa bbb ccc ddd\neee\n", "aaa bbb\nccc ddd\neee\n", 0);              // long lines are split, short ones not joined
    expect("-c -w 20", "tag: first line of it\n  second line goes on and on\n  third line too\n",
           "tag: first line of\n  it second line\n  goes on and on\n  third line too\n", 0);
    expect("-t -w 20", "tag: first line here\n  second line goes on and on\n",
           "tag: first line\n  here second line\n  goes on and on\n", 0);
    expect("-p > -w 20", "> a b c d e f g h i j k l m n o\nplain line\n",
           "> a b c d e f g h\n> i j k l m n o\nplain line\n", 0);                        // only the lines with the prefix

    printf("tabs and several files\n");
    expect("-w 30", "\tone two\n\tthree\n", "\tone two three\n", 0);                      // an indent of tabs is written as tabs
    write_text(workDir, "a.txt", "a b\nc d\n");
    write_text(workDir, "b.txt", "e f\ng h\n");
    expect("a.txt b.txt", "", "a b c d\ne f g h\n", 0);                                   // a paragraph ends with its file
    expect("a.txt - b.txt", "x\ny\n", "a b c d\nx y\ne f g h\n", 0);
    expect("", "one\r\ntwo\r\n", "one two\n", 0);                                         // a CR LF ends a line like LF

    printf("options and errors\n");
    message("-w x", "invalid width: 'x'", 1);
    message("-w 2501", "invalid width: '2501': Numerical result out of range", 1);
    message("-w 40 -g 50", "invalid width: '50': Numerical result out of range", 1);
    message("-g x", "invalid width: 'x'", 1);
    message("nosuchfile", "cannot open 'nosuchfile' for reading: No such file or directory", 1);
    message("--bogus", "unrecognized option", 1);
    expect_run("fmt", "--version", "", "fmt (Winshell) 0.1\n", 0);
    return test_summary("fmt_test");
}
