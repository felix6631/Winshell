/*
 * Tests of pr, run as the program it is: pages with header and trailer, page length, columns (down, across, side by side),
 * numbering, margins, separators, widths, tabs, control characters, form feeds, page ranges, and the errors. The header
 * date is set with -D, so the output does not depend on the time. GNU pr is compared with by gnu_compare.sh (more than a
 * thousand combinations); the expected texts here are the ones GNU writes.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("pr", arguments, input, expected, status);
}

/** The output for the arguments, checked for the number of lines and for what it starts and ends with. */
static void check_lines(const char* arguments, const char* input, int lines, const char* start, const char* end) {
    RunResult r;
    int count = 0;
    size_t n;

    run_tool(workDir, "pr", arguments, input, &r);
    CHECK_INT(r.exitCode, 0);
    n = r.out != NULL ? strlen(r.out) : 0;
    for (size_t i = 0; i < n; i++) {
        if (r.out[i] == '\n')
            count++;
    }
    CHECK_INT(count, lines);
    CHECK(r.out != NULL && strncmp(r.out, start, strlen(start)) == 0);
    CHECK(n >= strlen(end) && strcmp(r.out + n - strlen(end), end) == 0);
    run_result_free(&r);
}

static void message(const char* arguments, const char* input, const char* part, int status) {
    RunResult r;

    run_tool(workDir, "pr", arguments, input, &r);
    CHECK_INT(r.exitCode, status);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

int main(void) {
    tools_init("pr_test");
    reset_dir(workDir);
    write_text(workDir, "a.txt", "one\ntwo\nthree\n");
    write_text(workDir, "ten.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");
    write_text(workDir, "five.txt", "1\n2\n3\n4\n5\n");
    write_text(workDir, "other.txt", "21\n22\n23\n24\n");
    write_text(workDir, "ff.txt", "a\nb\n\fc\nd\n");
    write_text(workDir, "tabs.txt", "ab\tcd\n\tx\n   spaces   inside\n");
    write_text(workDir, "ctl.txt", "caf\303\251 \001 ctl \177 del\n");
    write_text(workDir, "long.txt", "abcdefghijklmnopqrstuvwxyz0123456789\nshort\n");
    write_text(workDir, "nonl.txt", "1\n2");

    printf("a page\n");
    // 66 lines: five of header, 56 of text (three of them lines of the file) and five of trailer
    check_lines("-D XX a.txt", "", 66, "\n\nXX", "\n\n\n\n\n\n");

    printf("pr cases\n");
    // pr -t a.txt
    expect("-t a.txt",
           "", "one\ntwo\nthree\n",
           0);
    // pr -T ff.txt
    expect("-T ff.txt",
           "", "a\nb\nc\nd\n",
           0);
    // pr -t ff.txt
    expect("-t ff.txt",
           "", "a\nb\n\fc\nd\n",
           0);
    // pr -D XX -l 12 ten.txt
    expect("-D XX -l 12 ten.txt",
           "", "\n\nXX                            ten.txt                             Page 1\n"
           "\n\n1\n2\n\n\n\n\n\n\n\nXX                            ten.txt                             Page 2\n"
           "\n\n3\n4\n\n\n\n\n\n\n\nXX                            ten.txt                             Page 3\n"
           "\n\n5\n6\n\n\n\n\n\n\n\nXX                            ten.txt                             Page 4\n"
           "\n\n7\n8\n\n\n\n\n\n\n\nXX                            ten.txt                             Page 5\n"
           "\n\n9\n10\n\n\n\n\n\n",
           0);
    // pr -D XX -f -l 12 five.txt
    expect("-D XX -f -l 12 five.txt",
           "", "\n\nXX                            five.txt                            Page 1\n"
           "\n\n1\n2\n\f\n\nXX                            five.txt                            Page 2\n"
           "\n\n3\n4\n\f\n\nXX                            five.txt                            Page 3\n"
           "\n\n5\n\f",
           0);
    // pr -D XX -h "My header" -l 11 a.txt
    expect("-D XX -h \"My header\" -l 11 a.txt",
           "", "\n\nXX                           My header                            Page 1\n"
           "\n\none\n\n\n\n\n\n\n\nXX                           My header                            Page 2\n"
           "\n\ntwo\n\n\n\n\n\n\n\nXX                           My header                            Page 3\n"
           "\n\nthree\n\n\n\n\n\n",
           0);
    // pr -D XX -h "" -l 11 a.txt
    expect("-D XX -h \"\" -l 11 a.txt",
           "", "\n\nXX                                                                Page 1\n"
           "\n\none\n\n\n\n\n\n\n\nXX                                                                Page 2\n"
           "\n\ntwo\n\n\n\n\n\n\n\nXX                                                                Page 3\n"
           "\n\nthree\n\n\n\n\n\n",
           0);
    // pr -t -n a.txt
    expect("-t -n a.txt",
           "", "    1\tone\n    2\ttwo\n    3\tthree\n",
           0);
    // pr -t -N 10 -n a.txt
    expect("-t -N 10 -n a.txt",
           "", "   10\tone\n   11\ttwo\n   12\tthree\n",
           0);
    // pr -t -n:3 a.txt
    expect("-t -n:3 a.txt",
           "", "  1:one\n  2:two\n  3:three\n",
           0);
    // pr -t -nx4 a.txt
    expect("-t -nx4 a.txt",
           "", "   1xone\n   2xtwo\n   3xthree\n",
           0);
    // pr -t -o 4 a.txt
    expect("-t -o 4 a.txt",
           "", "    one\n    two\n    three\n",
           0);
    // pr -t -o 4 -n a.txt
    expect("-t -o 4 -n a.txt",
           "", "        1\tone\n        2\ttwo\n        3\tthree\n",
           0);
    // pr -t -d a.txt
    expect("-t -d a.txt",
           "", "one\n\ntwo\n\nthree\n\n",
           0);
    // pr -t -3 ten.txt
    expect("-t -3 ten.txt",
           "", "1\t\t\t5\t\t\t8\n2\t\t\t6\t\t\t9\n3\t\t\t7\t\t\t10\n4\n",
           0);
    // pr -t -3 -a ten.txt
    expect("-t -3 -a ten.txt",
           "", "1\t\t\t2\t\t\t3\n4\t\t\t5\t\t\t6\n7\t\t\t8\t\t\t9\n10\n",
           0);
    // pr -t -2 -n five.txt
    expect("-t -2 -n five.txt",
           "", "    1\t1\t\t\t    \t4   4\n    2\t2\t\t\t    \t5   5\n    3\t3\n",
           0);
    // pr -t -2 -d five.txt
    expect("-t -2 -d five.txt",
           "", "1\t\t\t\t    4\n\n2\t\t\t\t    5\n\n3\n",
           0);
    // pr -t -2 -s five.txt
    expect("-t -2 -s five.txt",
           "", "1\t4\n2\t5\n3\n",
           0);
    // pr -t -2 -s: five.txt
    expect("-t -2 -s: five.txt",
           "", "1:4\n2:5\n3\n",
           0);
    // pr -t -2 "-S| " five.txt
    expect("-t -2 \"-S| \" five.txt",
           "", "1\t\t\t\t   | 4\n2\t\t\t\t   | 5\n3\n",
           0);
    // pr -t -2 -J five.txt
    expect("-t -2 -J five.txt",
           "", "1\t4\n2\t5\n3\n",
           0);
    // pr -t -2 -w 30 long.txt
    expect("-t -2 -w 30 long.txt",
           "", "abcdefghijklmn short\n",
           0);
    // pr -t -W 20 long.txt
    expect("-t -W 20 long.txt",
           "", "abcdefghijklmnopqrst\nshort\n",
           0);
    // pr -t -W 20 -n long.txt
    expect("-t -W 20 -n long.txt",
           "", "    1\tabcdefghijkl\n    2\tshort\n",
           0);
    // pr -t -m ten.txt other.txt
    expect("-t -m ten.txt other.txt",
           "", "1\t\t\t\t    21\n2\t\t\t\t    22\n3\t\t\t\t    23\n4\t\t\t\t    24\n5\t\t\t\t    \n"
           "6\t\t\t\t    \n7\t\t\t\t    \n8\t\t\t\t    \n9\t\t\t\t    \n10\t\t\t\t    \n",
           0);
    // pr -t -m -n five.txt other.txt
    expect("-t -m -n five.txt other.txt",
           "", "    1\t1\t\t\t\t21\n    2\t2\t\t\t\t22\n    3\t3\t\t\t\t23\n    4\t4\t\t\t\t24\n    5\t5\t\t\t\t\n",
           0);
    // pr -t -m -s five.txt other.txt
    expect("-t -m -s five.txt other.txt",
           "", "1\t21\n2\t22\n3\t23\n4\t24\n5\t\n",
           0);
    // pr -t -m -J five.txt other.txt
    expect("-t -m -J five.txt other.txt",
           "", "1\t21\n2\t22\n3\t23\n4\t24\n5\t\n",
           0);
    // pr -t -e tabs.txt
    expect("-t -e tabs.txt",
           "", "ab      cd\n        x\n   spaces   inside\n",
           0);
    // pr -t -e4 tabs.txt
    expect("-t -e4 tabs.txt",
           "", "ab  cd\n    x\n   spaces   inside\n",
           0);
    // pr -t -i tabs.txt
    expect("-t -i tabs.txt",
           "", "ab\tcd\n\tx\n   spaces   inside\n",
           0);
    // pr -t -c ctl.txt
    expect("-t -c ctl.txt",
           "", "caf\\303\\251 ^A ctl ^? del\n",
           0);
    // pr -t -v ctl.txt
    expect("-t -v ctl.txt",
           "", "caf\\303\\251 \\001 ctl \\177 del\n",
           0);
    // pr -D XX -l 12 +2 ten.txt
    expect("-D XX -l 12 +2 ten.txt",
           "", "\n\nXX                            ten.txt                             Page 2\n"
           "\n\n3\n4\n\n\n\n\n\n\n\nXX                            ten.txt                             Page 3\n"
           "\n\n5\n6\n\n\n\n\n\n\n\nXX                            ten.txt                             Page 4\n"
           "\n\n7\n8\n\n\n\n\n\n\n\nXX                            ten.txt                             Page 5\n"
           "\n\n9\n10\n\n\n\n\n\n",
           0);
    // pr -D XX -l 12 +2:2 ten.txt
    expect("-D XX -l 12 +2:2 ten.txt",
           "", "\n\nXX                            ten.txt                             Page 2\n"
           "\n\n3\n4\n\n\n\n\n\n",
           0);
    // pr -D XX -l 12 -n +2:3 ten.txt
    expect("-D XX -l 12 -n +2:3 ten.txt",
           "", "\n\nXX                            ten.txt                             Page 2\n"
           "\n\n    3\t3\n    4\t4\n\n\n\n\n\n\n\nXX                            ten.txt                             Page 3\n"
           "\n\n    5\t5\n    6\t6\n\n\n\n\n\n",
           0);
    // pr -D XX -l 12 -N 100 -n +2:2 ten.txt
    expect("-D XX -l 12 -N 100 -n +2:2 ten.txt",
           "", "\n\nXX                            ten.txt                             Page 2\n"
           "\n\n  100\t3\n  101\t4\n\n\n\n\n\n",
           0);
    // pr -D XX -l 20 ff.txt
    expect("-D XX -l 20 ff.txt",
           "", "\n\nXX                             ff.txt                             Page 1\n"
           "\n\na\nb\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\nXX                             ff.txt                             Page 2\n"
           "\n\nc\nd\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
           0);
    // pr -D XX -f -l 20 ff.txt
    expect("-D XX -f -l 20 ff.txt",
           "", "\n\nXX                             ff.txt                             Page 1\n"
           "\n\na\nb\n\f\n\nXX                             ff.txt                             Page 2\n"
           "\n\nc\nd\n\f",
           0);
    // pr -t nonl.txt
    expect("-t nonl.txt",
           "", "1\n2\n",
           0);
    // pr -D XX -l 14 -2 ten.txt
    expect("-D XX -l 14 -2 ten.txt",
           "", "\n\nXX                            ten.txt                             Page 1\n"
           "\n\n1\t\t\t\t    5\n2\t\t\t\t    6\n3\t\t\t\t    7\n4\t\t\t\t    8\n\n\n\n\n\n\n\nXX                            ten.txt                             Page 2\n"
           "\n\n9\t\t\t\t    10\n\n\n\n\n\n\n\n\n",
           0);
    // pr -D XX -w 40 a.txt
    expect("-D XX -w 40 a.txt",
           "", "\n\nXX             a.txt              Page 1\n\n\none\ntwo\nthree\n"
           "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"
           "\n",
           0);
    // pr -D XX a.txt five.txt
    expect("-D XX a.txt five.txt",
           "", "\n\nXX                             a.txt                              Page 1\n"
           "\n\none\ntwo\nthree\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"
           "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\nXX                            five.txt                            Page 1\n"
           "\n\n1\n2\n3\n4\n5\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"
           "\n\n\n\n\n\n\n\n\n\n\n",
           0);
    // pr -t -n a.txt five.txt
    expect("-t -n a.txt five.txt",
           "", "    1\tone\n    2\ttwo\n    3\tthree\n    1\t1\n    2\t2\n    3\t3\n    4\t4\n"
           "    5\t5\n",
           0);

    printf("standard input, the empty file\n");
    expect("-t", "x\ny\n", "x\ny\n", 0);
    expect("-t -", "x\ny\n", "x\ny\n", 0);
    expect("-t -2", "x\ny\nz\n", "x\t\t\t\t    z\ny\n", 0);
    expect("-D XX", "", "", 0);
    check_lines("-D XX", "x\ny\n", 66, "\n\n", "\n\n\n\n\n");
    check_lines("-D XX -f", "x\ny\n", 7, "\n\nXX", "y\n\f");
    expect("-t -n", "a\r\nb\r\n", "    1\ta\r\n    2\tb\r\n", 0);       // a CR is a character without width

    printf("errors\n");
    message("-t nosuchfile_zz", "", "pr: nosuchfile_zz: No such file or directory", 1);
    {
        RunResult r;

        run_tool(workDir, "pr", "-t -r nosuchfile_zz a.txt", "", &r);
        CHECK_INT(r.exitCode, 1);                       // -r leaves the message out, not the status
        CHECK(strstr(r.err, "nosuch") == NULL);
        CHECK(strstr(r.out, "one") != NULL);
        run_result_free(&r);
    }
    message("-l x a.txt", "", "'-l PAGE_LENGTH' invalid number of lines: 'x'", 1);
    message("-l 0 a.txt", "", "invalid number of lines: '0': Numerical result out of range", 1);
    message("-w 0 a.txt", "", "'-w PAGE_WIDTH' invalid number of characters: '0'", 1);
    message("-W x a.txt", "", "'-W PAGE_WIDTH' invalid number of characters: 'x'", 1);
    message("-o x a.txt", "", "'-o MARGIN' invalid line offset: 'x'", 1);
    message("-N x a.txt", "", "'-N NUMBER' invalid starting line number: 'x'", 1);
    message("-3 -w 3 a.txt", "", "page width too narrow", 1);
    message("-0 a.txt", "", "invalid number of columns: '0'", 1);
    message("-n,0 a.txt", "", "'-n' extra characters or invalid number in the argument: '0'", 1);
    message("-e,,3 a.txt", "", "'-e' extra characters or invalid number in the argument: ',3'", 1);
    message("+x a.txt", "", "invalid + argument 'x'", 1);
    message("-3 -m a.txt five.txt", "", "cannot specify number of columns when printing in parallel", 1);
    message("+3 a.txt", "", "starting page number 3 exceeds page count 1", 0);
    message("--bogus", "", "unrecognized option", 1);
    expect_run("pr", "--version", "", "pr (Winshell) 0.1\n", 0);
    return test_summary("pr_test");
}
