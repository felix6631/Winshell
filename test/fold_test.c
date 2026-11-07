/*
 * Tests of fold, run as the program it is: the width, -s, -b, tabs, backspaces, CR LF, UTF-8 characters and
 * several files. GNU fold is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("fold", arguments, input, expected, status);
}

int main(void) {
    tools_init("fold_test");

    printf("width\n");
    expect("-w 5", "abcdefghijkl\n", "abcde\nfghij\nkl\n", 0);
    expect("-w 5", "abcde\n", "abcde\n", 0);
    expect("-w 5", "abcdef", "abcde\nf", 0);               // no newline is added at the end
    expect("-5", "abcdefg\n", "abcde\nfg\n", 0);
    expect("--width=3", "abcdefg\n", "abc\ndef\ng\n", 0);
    expect("", "", "", 0);
    expect("-w 3", "\n\n", "\n\n", 0);
    expect("-w 1", "abc\n", "a\nb\nc\n", 0);

    printf("spaces\n");
    expect("-s -w 10", "hello world foo bar\n", "hello \nworld foo \nbar\n", 0);
    expect("-s -w 5", "abcdefghij kl\n", "abcde\nfghij\n kl\n", 0);
    expect("-sw 6", "ab cd ef gh\n", "ab cd \nef gh\n", 0);
    expect("-s -w 4", "a\tb c\n", "a\n\t\nb c\n", 0);

    printf("columns\n");
    expect("-w 8", "ab\tcd\n", "ab\t\ncd\n", 0);            // the tab goes to column 8, the c would be 9
    expect("-w 8", "abcdefg\tx\n", "abcdefg\t\nx\n", 0);
    expect("-w 4", "ab\bcde\n", "ab\bcde\n", 0);            // a backspace goes back one column
    expect("-w 3", "ab\rcdefgh\n", "ab\rcde\nfgh\n", 0);    // a carriage return goes back to the start
    expect("-b -w 4", "a\tbcde\n", "a\tbc\nde\n", 0);       // bytes: a tab is one
    expect("-w 3", "\xea\xb0\x80\xeb\x82\x98\xeb\x8b\xa4\xeb\x9d\xbc\n", "\xea\xb0\x80\xeb\x82\x98\xeb\x8b\xa4\n\xeb\x9d\xbc\n", 0);
    expect("-b -w 3", "\xea\xb0\x80\xeb\x82\x98\n", "\xea\xb0\x80\n\xeb\x82\x98\n", 0);

    printf("line endings and files\n");
    expect("-w 4", "abcdef\r\nghijkl\r\n", "abcd\nef\r\nghij\nkl\r\n", 0);
    write_text(workDir, "a.txt", "abcdefg");
    write_text(workDir, "b.txt", "hijklmn\n");
    expect("-w 3 a.txt b.txt", "", "abc\ndef\nghij\nklm\nn\n", 0);    // the end of the first file is not joined to the folding of the next
    expect("-w 3 - a.txt", "xyzw\n", "xyz\nw\nabc\ndef\ng", 0);

    printf("errors\n");
    expect("-w 0", "a\n", "", 1);
    expect("-w x", "a\n", "", 1);
    expect("-w -3", "a\n", "", 1);
    expect("nosuchfile", "", "", 1);
    expect("-x", "", "", 1);
    expect("--version", "", "fold (Winshell) 0.1\n", 0);

    return test_summary("fold_test");
}
