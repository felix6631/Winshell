/*
 * Tests of tee, tac, nl and paste, run as the programs they are.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void test_tee(void) {
    RunResult r;

    printf("tee\n");

    // the input goes to the output and to the files
    run_tool(workDir, "tee", "tee_a.txt tee_b.txt", "one\ntwo\n", &r);
    CHECK_STR(r.out, "one\ntwo\n");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    expect_content(workDir, "tee_a.txt", "one\ntwo\n");
    expect_content(workDir, "tee_b.txt", "one\ntwo\n");

    // overwritten, or appended with -a
    text_expect("tee", "tee_a.txt", "new\n", "new\n");
    expect_content(workDir, "tee_a.txt", "new\n");
    text_expect("tee", "-a tee_a.txt", "more\n", "more\n");
    expect_content(workDir, "tee_a.txt", "new\nmore\n");
    text_expect("tee", "--append tee_a.txt", "x", "x");
    expect_content(workDir, "tee_a.txt", "new\nmore\nx");

    // no file: a plain copy; bytes are not changed (CR LF, no final line feed, NUL)
    text_expect("tee", "", "a\r\nb", "a\r\nb");
    {
        RunResult n;
        char path[MAX_PATH * 2];
        bin_path("tee", path, sizeof path);
        run_program_ex(path, "tee_nul.bin", "a\0b", 3, NULL, workDir, 10000, &n);
        CHECK_INT((int)n.outLength, 3);
        CHECK(n.outLength == 3 && memcmp(n.out, "a\0b", 3) == 0);
        run_result_free(&n);
    }

    // a file that cannot be opened is reported, the others still get the data, and the status is 1
    make_directory("tee_dir");
    run_tool(workDir, "tee", "tee_dir tee_c.txt", "data\n", &r);
    CHECK_STR(r.out, "data\n");
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "tee_dir") != NULL);
    run_result_free(&r);
    expect_content(workDir, "tee_c.txt", "data\n");

    // virtual paths
    {
        char arguments[MAX_PATH * 2];
        snprintf(arguments, sizeof arguments, "%s/tee_v.txt", virtualWorkDir);
        text_expect("tee", arguments, "virtual\n", "virtual\n");
        expect_content(workDir, "tee_v.txt", "virtual\n");
    }

    run_tool(workDir, "tee", "--bogus", "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
}

static void test_tac(void) {
    RunResult r;

    printf("tac\n");

    text_expect("tac", "", "1\n2\n3\n", "3\n2\n1\n");
    text_expect("tac", "", "", "");
    text_expect("tac", "", "\n", "\n");
    text_expect("tac", "", "a\n\n\nb\n", "b\n\n\na\n");

    // a last line without its ending is joined to the one before it, as GNU tac does
    text_expect("tac", "", "a\nb", "ba\n");

    // CR LF stays with its line
    text_expect("tac", "", "a\r\nb\r\nc\r\n", "c\r\nb\r\na\r\n");
    text_expect("tac", "", "a\r\nb\nc\r\n", "c\r\nb\na\r\n");

    // -s, -b
    text_expect("tac", "-s :", "a:b:c:", "c:b:a:");
    text_expect("tac", "-s :", "a:b:c", "cb:a:");
    text_expect("tac", "-b -s :", ":a:b:c", ":c:b:a");
    text_expect("tac", "-b -s :", "a:b", ":ba");
    text_expect("tac", "-s ab", "1ab2ab3", "32ab1ab");
    text_expect("tac", "-bs ab", "1ab2ab3", "ab3ab21");

    // files, in the order given and each reversed
    write_text(workDir, "tac_1.txt", "a\nb\n");
    write_text(workDir, "tac_2.txt", "c\nd\n");
    text_expect("tac", "tac_1.txt tac_2.txt", "", "b\na\nd\nc\n");
    text_expect("tac", "tac_1.txt - tac_2.txt", "x\ny\n", "b\na\ny\nx\nd\nc\n");

    run_tool(workDir, "tac", "nosuch.txt tac_1.txt", "", &r);
    CHECK_STR(r.out, "b\na\n");
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "nosuch.txt") != NULL);
    run_result_free(&r);

    // an empty separator is a NUL byte, as in GNU tac
    {
        char path[MAX_PATH * 2];
        bin_path("tac", path, sizeof path);
        run_program_ex(path, "-s \"\"", "a\0b\0c", 5, NULL, workDir, 10000, &r);
        CHECK(r.outLength == 5 && memcmp(r.out, "cb\0a\0", 5) == 0);
        run_result_free(&r);
    }
    run_tool(workDir, "tac", "-r", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
}

static void test_nl(void) {
    RunResult r;

    printf("nl\n");

    // numbers the lines that are not empty, right justified in 6 columns, a tab after
    text_expect("nl", "", "a\nb\nc\n", "     1\ta\n     2\tb\n     3\tc\n");
    text_expect("nl", "", "a\n\nb\n", "     1\ta\n       \n     2\tb\n");
    text_expect("nl", "-ba", "a\n\nb\n", "     1\ta\n     2\t\n     3\tb\n");
    text_expect("nl", "-bn", "a\nb\n", "       a\n       b\n");
    text_expect("nl", "", "", "");

    // the line ending is kept: none at the end, CR LF, and a CR LF line is an empty one
    text_expect("nl", "", "a\nb", "     1\ta\n     2\tb\n");     // a last line gets an ending
    text_expect("nl", "", "a\r\nb\r\n", "     1\ta\r\n     2\tb\r\n");
    text_expect("nl", "", "a\r\n\r\nb\r\n", "     1\ta\r\n       \r\n     2\tb\r\n");
    text_expect("nl", "-ba", "a\r\n\r\nb\r\n", "     1\ta\r\n     2\t\r\n     3\tb\r\n");

    // formats, widths, separators, start and increment
    text_expect("nl", "-n ln", "a\nb\n", "1     \ta\n2     \tb\n");
    text_expect("nl", "-n rz", "a\nb\n", "000001\ta\n000002\tb\n");
    text_expect("nl", "-n rn -w 3", "a\n", "  1\ta\n");
    text_expect("nl", "-w 2 -s :", "a\n", " 1:a\n");
    text_expect("nl", "-v 10 -i 5", "a\nb\nc\n", "    10\ta\n    15\tb\n    20\tc\n");
    text_expect("nl", "-v -1", "a\nb\nc\n", "    -1\ta\n     0\tb\n     1\tc\n");
    text_expect("nl", "-w 1", "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\n", "1\ta\n2\tb\n3\tc\n4\td\n5\te\n6\tf\n7\tg\n8\th\n9\ti\n10\tj\n");
    text_expect("nl", "--number-format=ln --number-width=2 --number-separator=|", "a\n", "1 |a\n");

    // -l: groups of empty lines are counted as one (with -ba)
    text_expect("nl", "-ba -l 2", "a\n\n\n\nb\n", "     1\ta\n       \n     2\t\n       \n     3\tb\n");

    // logical pages: the header, body and footer delimiters are written as empty lines, and
    // the numbers start again at each header
    text_expect("nl", "", "a\n\\:\\:\\:\nh\n\\:\\:\nb\n\\:\nf\n", "     1\ta\n\n       h\n\n     1\tb\n\n       f\n");
    text_expect("nl", "-p", "a\n\\:\\:\\:\n\\:\\:\nb\n", "     1\ta\n\n\n     2\tb\n");
    text_expect("nl", "-ha -fa -d xy", "xyxyxy\nh\nxyxy\nb\nxy\nf\n", "\n     1\th\n\n     1\tb\n\n     1\tf\n");

    // files continue the numbers
    write_text(workDir, "nl_1.txt", "a\nb\n");
    write_text(workDir, "nl_2.txt", "c\n");
    text_expect("nl", "nl_1.txt nl_2.txt", "", "     1\ta\n     2\tb\n     3\tc\n");

    // errors
    run_tool(workDir, "nl", "-w 0", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
    run_tool(workDir, "nl", "-n xx", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
    run_tool(workDir, "nl", "-b x", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
    run_tool(workDir, "nl", "-bpa", "a\n", &r);          // the p style needs a pattern: not supported
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
    run_tool(workDir, "nl", "nosuch.txt", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "nosuch.txt") != NULL);
    run_result_free(&r);
}

static void test_paste(void) {
    RunResult r;

    printf("paste\n");

    write_text(workDir, "p_a.txt", "1\n2\n3\n");
    write_text(workDir, "p_b.txt", "a\nb\nc\n");
    write_text(workDir, "p_short.txt", "x\n");

    text_expect("paste", "p_a.txt p_b.txt", "", "1\ta\n2\tb\n3\tc\n");
    text_expect("paste", "p_a.txt p_b.txt p_short.txt", "", "1\ta\tx\n2\tb\t\n3\tc\t\n");
    text_expect("paste", "p_short.txt p_a.txt", "", "x\t1\n\t2\n\t3\n");
    text_expect("paste", "p_a.txt", "", "1\n2\n3\n");

    // delimiters are used in turn, and start again for each line
    text_expect("paste", "-d : p_a.txt p_b.txt", "", "1:a\n2:b\n3:c\n");
    text_expect("paste", "-d ,; p_a.txt p_b.txt p_a.txt", "", "1,a;1\n2,b;2\n3,c;3\n");
    text_expect("paste", "-d ,; p_a.txt p_b.txt p_a.txt p_b.txt", "", "1,a;1,a\n2,b;2,b\n3,c;3,c\n");
    text_expect("paste", "-d \"\\0\" p_a.txt p_b.txt", "", "1a\n2b\n3c\n");
    text_expect("paste", "-d \"\\t\\n\" p_a.txt p_b.txt p_a.txt", "", "1\ta\n1\n2\tb\n2\n3\tc\n3\n");

    // serial: one line per file
    text_expect("paste", "-s p_a.txt p_b.txt", "", "1\t2\t3\na\tb\tc\n");
    text_expect("paste", "-s -d , p_a.txt", "", "1,2,3\n");
    text_expect("paste", "-s -d ,: p_a.txt", "", "1,2:3\n");

    // standard input, which several "-" take line by line in turn
    text_expect("paste", "- -", "1\n2\n3\n4\n", "1\t2\n3\t4\n");
    text_expect("paste", "- -", "1\n2\n3\n", "1\t2\n3\t\n");
    text_expect("paste", "-d , - - -", "1\n2\n3\n4\n", "1,2,3\n4,,\n");
    text_expect("paste", "-s -d ,", "a\nb\nc\n", "a,b,c\n");
    text_expect("paste", "p_a.txt -", "x\ny\n", "1\tx\n2\ty\n3\t\n");

    // a missing final line feed does not matter; the output line always has one
    text_expect("paste", "- -", "1\n2", "1\t2\n");
    text_expect("paste", "-s", "a\nb", "a\tb\n");

    // CR LF: not part of a line, and the line written has the ending the input had
    text_expect("paste", "- -", "a\r\nb\r\n", "a\tb\r\n");
    text_expect("paste", "-s", "a\r\nb\r\n", "a\tb\r\n");
    text_expect("paste", "- -", "a\r\nb\n", "a\tb\r\n");

    // -z
    {
        RunResult n;
        char path[MAX_PATH * 2];
        bin_path("paste", path, sizeof path);
        run_program_ex(path, "-z - -", "a\0b\0", 4, NULL, workDir, 10000, &n);
        CHECK_INT((int)n.outLength, 4);
        CHECK(n.outLength == 4 && memcmp(n.out, "a\tb\0", 4) == 0);
        run_result_free(&n);
    }

    // errors: a missing file is reported, nothing is written, and the status is 1
    run_tool(workDir, "paste", "nosuch.txt p_a.txt", "", &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "nosuch.txt") != NULL);
    run_result_free(&r);
    run_tool(workDir, "paste", "-d \"a\\\\\"", "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
}

int main(void) {
    tools_init("lines_test");

    test_tee();
    test_tac();
    test_nl();
    test_paste();
    return test_summary("lines_test");
}
