/*
 * Tests of expand, run as the program it is: tab stops, lists, -i, backspaces, UTF-8 and several files.
 * GNU expand is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("expand", arguments, input, expected, status);
}

int main(void) {
    tools_init("expand_test");

    printf("tab size\n");
    expect("", "a\tb\n", "a       b\n", 0);
    expect("", "\tx\n", "        x\n", 0);
    expect("", "12345678\tx\n", "12345678        x\n", 0);
    expect("-t 4", "a\tb\tc\n", "a   b   c\n", 0);
    expect("--tabs=2", "a\tb\n", "a b\n", 0);
    expect("-4", "a\tb\n", "a   b\n", 0);
    expect("-t 1", "a\tb\n", "a b\n", 0);

    printf("lists\n");
    expect("-t 2,5", "a\tb\tc\td\n", "a b  c d\n", 0);          // after the last stop a tab is one blank
    expect("-t 3,/4", "\ta\tb\tc\n", "   a    b   c\n", 0);      // then multiples of 4
    expect("-t 3,+4", "\ta\tb\tc\n", "   a   b   c\n", 0);       // then every 4 after the last stop
    expect("-t 4 -t 8", "a\tb\tc\n", "a   b   c\n", 0);

    printf("initial blanks\n");
    expect("-i", "\ta\tb\n", "        a\tb\n", 0);
    expect("-i", "  \ta\tb\n", "        a\tb\n", 0);
    expect("--initial -t 4", "\t\tx\ty\n", "        x\ty\n", 0);

    printf("columns\n");
    expect("", "ab\bc\td\n", "ab\bc      d\n", 0);                 // a backspace goes back one column
    expect("", "\xea\xb0\x80\tx\n", "\xea\xb0\x80       x\n", 0);  // a character is one column
    expect("", "a\t", "a       ", 0);                             // no newline at the end

    printf("files and errors\n");
    write_text(workDir, "t.txt", "a\tb\n");
    expect("t.txt t.txt", "", "a       b\na       b\n", 0);
    expect("nosuchfile", "", "", 1);
    expect("-t 0", "a\n", "", 1);
    expect("-t x", "a\n", "", 1);
    expect("-t 4,2", "a\n", "", 1);
    expect("-x", "", "", 1);
    expect("--version", "", "expand (Winshell) 0.1\n", 0);

    return test_summary("expand_test");
}
