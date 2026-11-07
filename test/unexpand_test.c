/*
 * Tests of unexpand, run as the program it is: the blanks at the start of a line, -a, tab lists and the rule of
 * the single blank in front of a tab stop. GNU unexpand is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("unexpand", arguments, input, expected, status);
}

int main(void) {
    tools_init("unexpand_test");

    printf("initial blanks\n");
    expect("", "        x\n", "\tx\n", 0);
    expect("", "                x\n", "\t\tx\n", 0);
    expect("", "          x\n", "\t  x\n", 0);
    expect("", "  x\n", "  x\n", 0);
    expect("", "a       b\n", "a       b\n", 0);               // only the blanks at the start
    expect("", "\t   x\n", "\t   x\n", 0);
    expect("", "  \t  x\n", "\t  x\n", 0);

    printf("all blanks\n");
    expect("-a", "a       b\n", "a\tb\n", 0);
    expect("-a", "a      b\n", "a      b\n", 0);               // not reaching a tab stop
    expect("--all", "ab      cd       ef\n", "ab\tcd\t ef\n", 0);
    expect("-a", "       x\n", "       x\n", 0);               // a single blank before a tab stop stays
    expect("-a", "a       \tb\n", "a\t\tb\n", 0);
    expect("-a --first-only", "a       b\n        c\n", "a       b\n\tc\n", 0);
    expect("--first-only -a", "a       b\n", "a       b\n", 0);  // --first-only wins

    printf("a blank in front of a stop followed by more\n");
    expect("-a", "x      y  z\n", "x      y  z\n", 0);
    expect("-a", "ab      c\n", "ab\tc\n", 0);
    expect("-a", "xxxxxxx  y\n", "xxxxxxx\t y\n", 0);           // the blank before the stop is a tab when more follow

    printf("tab lists\n");
    expect("-t 4", "a   b\n", "a\tb\n", 0);
    expect("-t 4", "        x\n", "\t\tx\n", 0);
    expect("-t 2,8", "  a   b\n", "\ta   b\n", 0);
    expect("-t 4,8", "a                  b\n", "a\t\t           b\n", 0);   // after the last stop nothing is converted

    printf("files and errors\n");
    write_text(workDir, "t.txt", "        a\n");
    expect("t.txt", "", "\ta\n", 0);
    expect("nosuchfile", "", "", 1);
    expect("-t 0", "a\n", "", 1);
    expect("-t 4,2", "a\n", "", 1);
    expect("-x", "", "", 1);
    expect("--version", "", "unexpand (Winshell) 0.1\n", 0);

    return test_summary("unexpand_test");
}
