/*
 * Tests of the control structures of the shell, run as the program it is: if, while, until, for, break and
 * continue, lists across lines, comments, and what is reported for what cannot be parsed.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void run_shell(const char* arguments, const char* input, RunResult* r) {
    CHECK(run_program(shellPath, arguments, input, workDir, r) == 0);
    CHECK(!r->timedOut);
}

/** Runs the text as a script file and compares the output and the exit status. */
static void expect(const char* name, const char* text, const char* arguments, const char* expected, int status) {
    RunResult r;
    char command[MAX_PATH * 2];

    write_text(workDir, name, text);
    snprintf(command, sizeof command, "%s %s", name, arguments);
    run_shell(command, "", &r);
    if (r.out != NULL)
        lf_only(r.out);
    if (r.out == NULL || strcmp(r.out, expected) != 0)
        printf("    for: shell %s\n    got: \"%s\"\n    err: \"%s\"\n", command, r.out == NULL ? "(null)" : r.out,
               r.err == NULL ? "" : r.err);
    CHECK_STR(r.out, expected);
    CHECK_INT(r.exitCode, status);
    run_result_free(&r);
}

static void test_if(void) {
    printf("if\n");
    expect("i1.sh", "if true; then echo yes; fi\n", "", "yes\n", 0);
    expect("i2.sh", "if false; then echo yes; else echo no; fi\n", "", "no\n", 0);
    expect("i3.sh", "if false; then echo a; elif true; then echo b; else echo c; fi\n", "", "b\n", 0);
    expect("i4.sh", "if false; then echo a; elif false; then echo b; else echo c; fi\n", "", "c\n", 0);
    expect("i5.sh", "if false; then echo a; fi\necho $?\n", "", "0\n", 0);

    // on several lines
    expect("i6.sh", "if [ 1 -eq 1 ]\nthen\n  echo one\n  echo two\nelse\n  echo no\nfi\n", "", "one\ntwo\n", 0);

    // the condition is a list: the status of the last command counts
    expect("i7.sh", "if false; true; then echo last; fi\nif true && false; then echo and; else echo not; fi\n", "",
           "last\nnot\n", 0);

    // nested, and in a list
    expect("i8.sh", "if true; then if false; then echo a; else echo inner; fi; fi\n", "", "inner\n", 0);
    expect("i9.sh", "if true; then echo a; fi && echo b\nif false; then echo c; fi || echo d\n", "", "a\nb\n", 0);

    // the status of the if is that of the branch that ran
    expect("i10.sh", "if true; then expr 1 = 2; fi\necho $?\n", "", "0\n1\n", 0);

    // reserved words are only that as the first word; quoted they are words
    expect("i11.sh", "echo if then fi\necho 'if' \"then\"\n\"if\" true 2> nul; echo $?\n", "", "if then fi\nif then\n127\n", 0);

    // exit inside
    expect("i12.sh", "if true; then echo a; exit 5; echo b; fi\necho c\n", "", "a\n", 5);
    // a comment after the condition, and an empty line in a body
    expect("i13.sh", "if true # why\nthen # now\n\n  echo ok\nfi\n", "", "ok\n", 0);
}

static void test_loops(void) {
    printf("while, until, for\n");
    expect("w1.sh", "n=0\nwhile [ $n -lt 3 ]; do echo $n; n=$(expr $n + 1); done\n", "", "0\n1\n2\n", 0);
    expect("w2.sh", "n=3\nuntil [ $n -eq 0 ]; do n=$(expr $n - 1); echo $n; done\n", "", "2\n1\n0\n", 0);
    expect("w3.sh", "while false; do echo never; done\necho $?\n", "", "0\n", 0);
    expect("w4.sh", "n=2\nwhile [ $n -gt 0 ]; do\n  n=$(expr $n - 1)\n  expr $n + 10\ndone\necho $?\n", "", "11\n10\n0\n", 0);

    expect("f1.sh", "for x in a b c; do echo \"[$x]\"; done\n", "", "[a]\n[b]\n[c]\n", 0);
    expect("f2.sh", "for x in 'a b' c; do echo \"[$x]\"; done\n", "", "[a b]\n[c]\n", 0);
    expect("f3.sh", "v=\"1 2 3\"\nfor x in $v; do echo $x; done\n", "", "1\n2\n3\n", 0);
    expect("f4.sh", "for x in $(echo p q); do echo $x; done\n", "", "p\nq\n", 0);
    expect("f5.sh", "for x in; do echo never; done\necho $?\n", "", "0\n", 0);
    expect("f6.sh", "for p; do echo \"<$p>\"; done\n", "\"a b\" c", "<a b>\n<c>\n", 0);
    expect("f7.sh", "for x in 1 2\ndo\n  echo $x\ndone\n", "", "1\n2\n", 0);
    expect("f8.sh", "for x in 1 2; do y=$x; done\necho $x $y\n", "", "2 2\n", 0);

    // wildcards expand
    write_text(workDir, "g_a.txt", "");
    write_text(workDir, "g_b.txt", "");
    expect("f9.sh", "for f in g_*.txt; do echo $f; done\n", "", "g_a.txt\ng_b.txt\n", 0);

    // nested loops, a loop inside an if
    expect("n1.sh", "for a in 1 2; do for b in x y; do echo $a$b; done; done\n", "", "1x\n1y\n2x\n2y\n", 0);
    expect("n2.sh", "for a in 1 2 3; do if [ $a -eq 2 ]; then echo two; else echo other; fi; done\n", "",
           "other\ntwo\nother\n", 0);

    // exit inside a loop ends the shell; the status of a loop is that of its last round
    expect("e1.sh", "for a in 1 2 3; do echo $a; if [ $a -eq 2 ]; then exit 4; fi; done\necho after\n", "", "1\n2\n", 4);
    expect("e2.sh", "for a in 1 2; do expr $a = 2; done\necho $?\n", "", "0\n1\n0\n", 0);
}

static void test_break_continue(void) {
    printf("break and continue\n");
    expect("b1.sh", "for i in 1 2 3 4; do if [ $i -eq 3 ]; then break; fi; echo $i; done\n", "", "1\n2\n", 0);
    expect("b2.sh", "for i in 1 2 3 4; do if [ $i -eq 2 ]; then continue; fi; echo $i; done\n", "", "1\n3\n4\n", 0);
    expect("b3.sh", "n=0\nwhile true; do n=$(expr $n + 1); if [ $n -gt 2 ]; then break; fi; echo $n; done\n", "",
           "1\n2\n", 0);
    expect("b4.sh", "for a in 1 2; do for b in x y z; do if [ $b = y ]; then break 2; fi; echo $a$b; done; done\necho end\n",
           "", "1x\nend\n", 0);
    expect("b5.sh", "for a in 1 2; do for b in x y; do if [ $b = x ]; then continue 2; fi; echo $a$b; done; done\necho end\n",
           "", "end\n", 0);
    // the rest of the body is skipped, and so is the rest of the list
    expect("b6.sh", "for a in 1 2; do echo $a; break; echo no; done; echo out\n", "", "1\nout\n", 0);
    expect("b7.sh", "for a in 1 2; do continue && echo no; echo no; done; echo out\n", "", "out\n", 0);

    // outside of a loop: a message, status 1, the script goes on
    {
        RunResult r;

        write_text(workDir, "b8.sh", "break\necho $?\ncontinue\necho $?\n");
        run_shell("b8.sh", "", &r);
        lf_only(r.out);
        CHECK_STR(r.out, "1\n1\n");
        CHECK(strstr(r.err, "only meaningful in a") != NULL);
        run_result_free(&r);
    }

    // : does nothing, successfully
    expect("c1.sh", ":\necho $?\nwhile :; do break; done; echo out\n", "", "0\nout\n", 0);
}

static void test_syntax(void) {
    RunResult r;

    printf("syntax\n");
    // errors are reported; what was before the error in the text has run, what is after it has not
    write_text(workDir, "x1.sh", "echo before\nfi\necho after\n");
    run_shell("x1.sh", "", &r);
    lf_only(r.out);
    CHECK_STR(r.out, "before\n");
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "unexpected token 'fi'") != NULL);
    run_result_free(&r);

    write_text(workDir, "x2.sh", "if true; then echo a\n");
    run_shell("x2.sh", "", &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "unexpected end of input") != NULL);
    run_result_free(&r);

    write_text(workDir, "x3.sh", "while true; do done\n");
    run_shell("x3.sh", "", &r);
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "missing command after 'do'") != NULL);
    run_result_free(&r);

    write_text(workDir, "x4.sh", "for 1x in a; do echo; done\n");
    run_shell("x4.sh", "", &r);
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);

    write_text(workDir, "x5.sh", "if true; echo a; fi\n");
    run_shell("x5.sh", "", &r);
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "unexpected token 'fi'") != NULL);
    run_result_free(&r);

    write_text(workDir, "x6.sh", "echo a; ; echo b\n");
    run_shell("x6.sh", "", &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);

    // from standard input a compound command may go on in the next lines (and nothing runs until it is complete)
    run_shell("", "if true\nthen\n  echo from-input\nfi\necho next\n", &r);
    lf_only(r.out);
    CHECK(strstr(r.out, "from-input") != NULL);
    CHECK(strstr(r.out, "next") != NULL);
    run_result_free(&r);

    run_shell("-c \"for i in 1 2; do echo $i; done; echo done\"", "", &r);
    lf_only(r.out);
    CHECK_STR(r.out, "1\n2\ndone\n");
    run_result_free(&r);
}

static void test_input(void) {
    printf("read, redirections of compound commands, pipes into loops\n");
    write_text(workDir, "data.txt", "alpha 1\nbeta 2\ngamma 3\n");

    expect("r1.sh", "while read name num; do echo \"$name=$num\"; done < data.txt\n", "", "alpha=1\nbeta=2\ngamma=3\n", 0);
    expect("r2.sh", "total=0\nwhile read name num; do total=$((total+num)); done < data.txt\necho $total\n", "", "6\n", 0);
    expect("r3.sh", "cat data.txt | while read line; do echo \"<$line>\"; done\n", "", "<alpha 1>\n<beta 2>\n<gamma 3>\n", 0);
    expect("r4.sh", "echo \"a b c d\" | while read p q; do echo \"$p|$q\"; done\n", "", "a|b c d\n", 0);
    expect("r5.sh", "echo \"  x  \" | while read v; do echo \"[$v]\"; done\n", "", "[x]\n", 0);
    expect("r6.sh", "echo k:v:w | while IFS=: read a b c; do echo \"$a $b $c\"; done\necho \"IFS is $IFS|\"\n", "",
           "k v w\nIFS is |\n", 0);
    expect("r7.sh", "echo whole line | while read; do echo \"[$REPLY]\"; done\n", "", "[whole line]\n", 0);
    expect("r8.sh", "echo one | while read -r first second; do echo \"$first/$second\"; done\n", "", "one/\n", 0);

    // the status of read: 1 at the end of the input
    // the status of a loop is that of its body (the read that ended it is the condition)
    expect("r9.sh", "while read l; do :; done < data.txt\necho $?\n", "", "0\n", 0);
    expect("r9b.sh", "read a < data.txt\necho $?\nread z < nosuch.txt 2> nul\necho $?\n", "", "0\n1\n", 0);
    expect("r10.sh", "read x < data.txt\necho \"$x\"\n", "", "alpha 1\n", 0);

    // a loop that ends early leaves the rest of the input unread
    expect("r11.sh", "cat data.txt | while read l w; do if [ $w -eq 2 ]; then break; fi; echo $l; done\necho end\n", "",
           "alpha\nend\n", 0);

    // redirections after the end of a compound command
    expect("o1.sh", "for i in 1 2 3; do echo item $i; done > loop.txt\ncat loop.txt\n", "", "item 1\nitem 2\nitem 3\n", 0);
    expect("o2.sh", "for i in 1 2; do echo a$i; done > append.txt\nfor i in 3; do echo a$i; done >> append.txt\ncat append.txt\n",
           "", "a1\na2\na3\n", 0);
    expect("o3.sh", "if true; then echo out; echo err 1>&2; fi > both.txt 2>&1\ncat both.txt\n", "", "out\nerr\n", 0);
    expect("o4.sh", "while true; do echo no; break; done > nul\necho visible\n", "", "visible\n", 0);
    expect("o5.sh", "n=0\nwhile read l; do n=$((n+1)); done < data.txt > count.txt\ncat count.txt\necho $n\n", "", "3\n", 0);

    // programs started in a redirected loop read the same input
    expect("o6.sh", "while read l w; do echo \"$l\"; done < data.txt | cat\n", "", "", 2);

    // a file that cannot be opened: a message, status 1, the loop does not run
    {
        RunResult r;

        write_text(workDir, "o7.sh", "while read l; do echo never; done < nosuch.txt\necho \"status $?\"\n");
        run_shell("o7.sh", "", &r);
        lf_only(r.out);
        CHECK_STR(r.out, "status 1\n");
        CHECK(strstr(r.err, "Cannot open 'nosuch.txt'") != NULL);
        run_result_free(&r);
    }
}

int main(void) {
    tools_init("shell_control_test");
    test_if();
    test_loops();
    test_break_continue();
    test_input();
    test_syntax();
    return test_summary("shell_control_test");
}
