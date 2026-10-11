/*
 * Tests of head, tail and wc.
 */

#include "testtools.h"

static void test_text_tools(void) {
    RunResult r;
    char lines[256], crlf[256];
    int i;

    printf("head, tail and wc\n");

    // ten numbered lines, with LF and with CR LF
    lines[0] = crlf[0] = '\0';
    for (i = 1; i <= 12; i++) {
        char one[32];
        snprintf(one, sizeof one, "l%d\n", i);
        strcat(lines, one);
        snprintf(one, sizeof one, "c%d\r\n", i);
        strcat(crlf, one);
    }
    write_text(workDir, "lines.txt", lines);
    write_text(workDir, "crlf12.txt", crlf);
    write_text(workDir, "nonl.txt", "a\nb\nc");
    write_text(workDir, "empty.txt", "");

    // head
    text_expect("head", "lines.txt", NULL, "l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\n");
    text_expect("head", "-n 2 lines.txt", NULL, "l1\nl2\n");
    text_expect("head", "-n2 lines.txt", NULL, "l1\nl2\n");
    text_expect("head", "-2 lines.txt", NULL, "l1\nl2\n");
    text_expect("head", "--lines=2 lines.txt", NULL, "l1\nl2\n");
    text_expect("head", "-n 0 lines.txt", NULL, "");
    text_expect("head", "-n -10 lines.txt", NULL, "l1\nl2\n");
    text_expect("head", "-n 100 nonl.txt", NULL, "a\nb\nc");
    text_expect("head", "-n -1 nonl.txt", NULL, "a\nb\n");
    text_expect("head", "-c 5 lines.txt", NULL, "l1\nl2");
    text_expect("head", "-c -30 lines.txt", NULL, "l1\nl2\nl3\n");           // the file is 39 bytes
    text_expect("head", "-c 1k nonl.txt", NULL, "a\nb\nc");
    text_expect("head", "-n 3", "x\ny\nz\nw\n", "x\ny\nz\n");
    text_expect("head", "-n 2 -", "x\ny\nz\n", "x\ny\n");
    text_expect("head", "-n 3 empty.txt", NULL, "");
    // CR LF is kept, and counts as one line ending
    text_expect("head", "-n 2 crlf12.txt", NULL, "c1\r\nc2\r\n");
    text_expect("head", "-n -10 crlf12.txt", NULL, "c1\r\nc2\r\n");
    // more than one file: headers
    text_expect("head", "-n 1 lines.txt nonl.txt", NULL, "==> lines.txt <==\nl1\n\n==> nonl.txt <==\na\n");
    text_expect("head", "-q -n 1 lines.txt nonl.txt", NULL, "l1\na\n");
    text_expect("head", "-v -n 1 lines.txt", NULL, "==> lines.txt <==\nl1\n");
    text_expect("head", "-n 1 - lines.txt", "in\nput\n", "==> standard input <==\nin\n\n==> lines.txt <==\nl1\n");

    run_tool(workDir, "head", "nosuch.txt lines.txt -n 1", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "head: cannot open 'nosuch.txt' for reading: No such file or directory") != NULL);
    run_result_free(&r);
    run_tool(workDir, "head", "-n x lines.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "head: invalid number of lines: 'x'") != NULL);
    run_result_free(&r);

    // tail
    text_expect("tail", "lines.txt", NULL, "l3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\n");
    text_expect("tail", "-n 2 lines.txt", NULL, "l11\nl12\n");
    text_expect("tail", "-2 lines.txt", NULL, "l11\nl12\n");
    text_expect("tail", "-n 0 lines.txt", NULL, "");
    text_expect("tail", "-n 100 nonl.txt", NULL, "a\nb\nc");
    text_expect("tail", "-n 2 nonl.txt", NULL, "b\nc");               // the last line has no line feed
    text_expect("tail", "-n +11 lines.txt", NULL, "l11\nl12\n");
    text_expect("tail", "-n +1 nonl.txt", NULL, "a\nb\nc");
    text_expect("tail", "-n +100 lines.txt", NULL, "");
    text_expect("tail", "-c 5 lines.txt", NULL, "\nl12\n");
    text_expect("tail", "-c +32 lines.txt", NULL, "l11\nl12\n");
    text_expect("tail", "-c +30 lines.txt", NULL, "0\nl11\nl12\n");
    text_expect("tail", "-c 0 lines.txt", NULL, "");
    text_expect("tail", "-n 2", "x\ny\nz\n", "y\nz\n");
    text_expect("tail", "-n 2 -", "x\ny\nz", "y\nz");
    text_expect("tail", "-n +2", "x\ny\nz\n", "y\nz\n");
    text_expect("tail", "-n 3 empty.txt", NULL, "");
    text_expect("tail", "-n 2 crlf12.txt", NULL, "c11\r\nc12\r\n");
    text_expect("tail", "-n +12 crlf12.txt", NULL, "c12\r\n");
    text_expect("tail", "-n 1 lines.txt nonl.txt", NULL, "==> lines.txt <==\nl12\n\n==> nonl.txt <==\nc");
    text_expect("tail", "-q -n 1 lines.txt nonl.txt", NULL, "l12\nc");
    text_expect("tail", "-v -n 1 lines.txt", NULL, "==> lines.txt <==\nl12\n");

    // a file longer than the blocks it is read in
    {
        size_t length = 0;
        char* big = malloc(400000);
        char* p = big;
        for (i = 0; i < 40000; i++)
            p += sprintf(p, "row %05d\n", i);
        length = (size_t)(p - big);
        write_file(workDir, "big.txt", big, length);
        free(big);

        text_expect("tail", "-n 3 big.txt", NULL, "row 39997\nrow 39998\nrow 39999\n");
        text_expect("tail", "-n +39999 big.txt", NULL, "row 39998\nrow 39999\n");
        text_expect("head", "-n 2 big.txt", NULL, "row 00000\nrow 00001\n");
        text_expect("head", "-n -39998 big.txt", NULL, "row 00000\nrow 00001\n");
        text_expect("tail", "-c 20 big.txt", NULL, "row 39998\nrow 39999\n");
    }

    run_tool(workDir, "tail", "nosuch.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "tail: cannot open 'nosuch.txt' for reading: No such file or directory") != NULL);
    run_result_free(&r);

    // wc
    write_text(workDir, "words.txt", "one two  three\nfour\tfive\n\nsix");
    // 29 bytes: more than one number of one file are as wide as the size of the file
    text_expect("wc", "words.txt", NULL, " 3  6 29 words.txt\n");
    text_expect("wc", "-l words.txt", NULL, "3 words.txt\n");
    text_expect("wc", "-w words.txt", NULL, "6 words.txt\n");
    text_expect("wc", "-c words.txt", NULL, "29 words.txt\n");
    text_expect("wc", "-m words.txt", NULL, "29 words.txt\n");
    text_expect("wc", "-L words.txt", NULL, "14 words.txt\n");
    text_expect("wc", "-lw words.txt", NULL, " 3  6 words.txt\n");
    text_expect("wc", "-wl words.txt", NULL, " 3  6 words.txt\n");       // always in the same order
    text_expect("wc", "-l", "a\nb\nc\n", "3\n");
    // standard input that is a file: as wide as the size of the file; here (a file) the numbers are not padded
    text_expect("wc", "", "a b\nc\n", "2 3 6\n");
    text_expect("wc", "-", "a b\nc\n", "2 3 6 -\n");
    text_expect("wc", "-l -", "a\nb\n", "2 -\n");
    text_expect("wc", "empty.txt", NULL, "0 0 0 empty.txt\n");

    // CR LF: the same number of lines, a CR is white space and no width
    write_text(workDir, "wcr.txt", "a b\r\nc\r\n");
    text_expect("wc", "wcr.txt", NULL, "2 3 8 wcr.txt\n");
    text_expect("wc", "-L wcr.txt", NULL, "3 wcr.txt\n");
    text_expect("wc", "-L lines.txt", NULL, "3 lines.txt\n");

    // characters of UTF-8
    write_text(workDir, "utf8.txt", "caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac\n");
    text_expect("wc", "-m utf8.txt", NULL, "8 utf8.txt\n");        // c a f é, a space, two ideographs, the line feed
    text_expect("wc", "-c utf8.txt", NULL, "13 utf8.txt\n");
    text_expect("wc", "-w utf8.txt", NULL, "2 utf8.txt\n");

    // several files: padded to the size of the whole, and a total
    write_text(workDir, "w1.txt", "a\nb\n");
    write_text(workDir, "w2.txt", "c\n");
    text_expect("wc", "-l w1.txt w2.txt", NULL, "2 w1.txt\n1 w2.txt\n3 total\n");
    text_expect("wc", "w1.txt w2.txt", NULL, "2 2 4 w1.txt\n1 1 2 w2.txt\n3 3 6 total\n");

    run_tool(workDir, "wc", "nosuch.txt w2.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "wc: nosuch.txt: No such file or directory") != NULL);
    CHECK(strstr(r.out, "w2.txt") != NULL && strstr(r.out, "total") != NULL);
    run_result_free(&r);

    run_tool(workDir, "wc", ".", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "wc: .: Is a directory") != NULL);
    run_result_free(&r);

    run_tool(workDir, "wc", "-x", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    run_tool(workDir, "head", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: head") != NULL);
    run_result_free(&r);
    run_tool(workDir, "tail", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: tail") != NULL);
    run_result_free(&r);
    run_tool(workDir, "wc", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: wc") != NULL);
    run_result_free(&r);
}

int main(void) {
    tools_init("text_test");

    test_text_tools();
    return test_summary("text_test");
}
