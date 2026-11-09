/*
 * Tests of join, run as the program it is: pairing on the first field, other fields and separators, -a -v -e -o, -t,
 * the order check, --header and the errors. CR LF input is read as lines (GNU keeps the CR in the last field).
 * GNU join is compared with by gnu_compare.sh (also on random inputs).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("join", arguments, input, expected, status);
}

int main(void) {
    RunResult r;

    tools_init("join_test");
    reset_dir(workDir);
    write_text(workDir, "a.txt", "a 1\nb 2\nc 3\n");
    write_text(workDir, "b.txt", "a x\nc y\nd z\n");

    printf("pairing\n");
    expect("a.txt b.txt", "", "a 1 x\nc 3 y\n", 0);
    expect("b.txt a.txt", "", "a x 1\nc y 3\n", 0);
    expect("-a1 a.txt b.txt", "", "a 1 x\nb 2\nc 3 y\n", 0);
    expect("-a1 -a2 a.txt b.txt", "", "a 1 x\nb 2\nc 3 y\nd z\n", 0);
    expect("-v1 a.txt b.txt", "", "b 2\n", 0);
    expect("-v2 a.txt b.txt", "", "d z\n", 0);
    expect("-v1 -v2 a.txt b.txt", "", "b 2\nd z\n", 0);
    expect("-o 0,2.2,1.2 a.txt b.txt", "", "a x 1\nc y 3\n", 0);
    expect("-e X -o 0,1.2,2.2 -a1 -a2 a.txt b.txt", "", "a 1 x\nb 2 X\nc 3 y\nd X z\n", 0);
    expect("-o auto -a1 -a2 -e - a.txt b.txt", "", "a 1 x\nb 2 -\nc 3 y\nd - z\n", 0);
    expect("- a.txt", "a q\nb r\n", "a q 1\nb r 2\n", 0);

    printf("every line with every line\n");
    write_text(workDir, "da.txt", "a 1\na 2\nb 3\n");
    write_text(workDir, "db.txt", "a x\na y\nb z\n");
    expect("da.txt db.txt", "", "a 1 x\na 1 y\na 2 x\na 2 y\nb 3 z\n", 0);

    printf("fields and separators\n");
    write_text(workDir, "c1.txt", "a,1,2\nb,3,4\n");
    write_text(workDir, "c2.txt", "a,x\nb,y\n");
    expect("-t, c1.txt c2.txt", "", "a,1,2,x\nb,3,4,y\n", 0);
    expect("-t, -1 2 -2 1 c1.txt c2.txt", "", "", 0);
    write_text(workDir, "k1.txt", "1 a\n2 b\n");
    write_text(workDir, "k2.txt", "x 1\ny 2\n");
    expect("-1 1 -2 2 k1.txt k2.txt", "", "1 a x\n2 b y\n", 0);
    expect("-j 2 k2.txt k2.txt", "", "1 x x\n2 y y\n", 0);        // joined on field 2 of both: the key is the 1 and the 2
    write_text(workDir, "bl.txt", "a b \n");
    expect("bl.txt bl.txt", "", "a b  b \n", 0);                    // a blank at the end makes an empty last field
    write_text(workDir, "ci1.txt", "a 1\nB 2\n");
    write_text(workDir, "ci2.txt", "A x\nb y\n");
    expect("-i ci1.txt ci2.txt", "", "a 1 x\nB 2 y\n", 0);

    printf("CR LF\n");
    write_text(workDir, "crlf1.txt", "a 1\r\nb 2\r\nc 3\r\n");
    write_text(workDir, "crlf2.txt", "a x\r\nc y\r\n");
    expect("crlf1.txt crlf2.txt", "", "a 1 x\r\nc 3 y\r\n", 0);
    expect("crlf1.txt a.txt", "", "a 1 1\nb 2 2\nc 3 3\n", 0);           // the line written ends as the line of the second file

    printf("the order\n");
    write_text(workDir, "un.txt", "b 1\na 2\n");
    run_tool(workDir, "join", "a.txt un.txt", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK_STR(r.out, "b 2 1\n");
    CHECK(strstr(r.err, "un.txt:2: is not sorted: a 2") != NULL);
    CHECK(strstr(r.err, "input is not in sorted order") != NULL);
    run_result_free(&r);
    run_tool(workDir, "join", "--check-order a.txt un.txt", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK_STR(r.out, "");
    run_result_free(&r);
    expect("--nocheck-order a.txt un.txt", "", "b 2 1\n", 0);

    printf("--header\n");
    write_text(workDir, "h1.txt", "k v\na 1\nb 2\n");
    write_text(workDir, "h2.txt", "k w\na 3\nc 4\n");
    expect("--header h1.txt h2.txt", "", "k v w\na 1 3\n", 0);

    printf("errors\n");
    expect("a.txt", "", "", 1);
    expect("", "", "", 1);
    expect("a.txt b.txt a.txt", "", "", 1);
    expect("- -", "", "", 1);
    expect("a.txt nosuchfile", "", "", 1);
    expect("-j 0 a.txt b.txt", "", "", 1);
    expect("-1 x a.txt b.txt", "", "", 1);
    expect("-a 3 a.txt b.txt", "", "", 1);
    expect("-o 1 a.txt b.txt", "", "", 1);
    expect("-o 3.1 a.txt b.txt", "", "", 1);
    expect("-t ab a.txt b.txt", "", "", 1);
    expect("-t , -t ; a.txt b.txt", "", "", 1);
    expect("--bogus a.txt b.txt", "", "", 1);
    expect("--version", "", "join (Winshell) 0.1\n", 0);

    return test_summary("join_test");
}
