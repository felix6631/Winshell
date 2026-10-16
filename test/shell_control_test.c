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
    expect("o6.sh", "while read l w; do echo \"$l\"; done < data.txt | cat\n", "", "alpha\nbeta\ngamma\n", 0);

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

static void test_case(void) {
    printf("case\n");
    expect("k1.sh", "case abc in abc) echo exact ;; esac\n", "", "exact\n", 0);
    expect("k2.sh", "case x.txt in *.c) echo c ;; *.txt) echo txt ;; *) echo other ;; esac\n", "", "txt\n", 0);
    expect("k3.sh", "case b in a|b|c) echo abc ;; esac\n", "", "abc\n", 0);
    expect("k4.sh", "case none in a) echo a ;; b) echo b ;; esac\necho $?\n", "", "0\n", 0);
    expect("k5.sh", "v=\"a b\"\ncase \"$v\" in \"a b\") echo quoted ;; *) echo no ;; esac\n", "", "quoted\n", 0);
    expect("k6.sh", "case 5 in [0-9]) echo digit ;; esac\ncase a in [!0-9]) echo notdigit ;; esac\n", "",
           "digit\nnotdigit\n", 0);

    // quoted wildcards are plain; the first clause that matches is the only one that runs
    expect("k7.sh", "case abc in \"a*\") echo literal ;; a*) echo glob ;; esac\ncase \"*\" in \"*\") echo star ;; esac\n", "",
           "glob\nstar\n", 0);
    expect("k8.sh", "case a in a) echo first ;; a) echo second ;; esac\n", "", "first\n", 0);

    // the word is expanded; clauses on several lines; the last clause needs no ;;
    expect("k9.sh", "f=file.c\ncase $f in\n  *.c)\n    echo c source\n    echo second line\n    ;;\n  *)\n    echo other\nesac\n",
           "", "c source\nsecond line\n", 0);
    expect("k10.sh", "case $(echo hi) in hi) echo from-substitution ;; esac\ncase x in (x) echo paren ;; esac\n", "",
           "from-substitution\nparen\n", 0);

    // inside a loop, with break
    expect("k11.sh", "for w in a b stop c; do case $w in stop) break ;; *) echo $w ;; esac; done\n", "", "a\nb\n", 0);
}

static void test_functions(void) {
    printf("functions, groups, subshells\n");
    expect("fn1.sh", "hello() { echo \"hello $1\"; }\nhello world\nhello there\n", "", "hello world\nhello there\n", 0);
    expect("fn2.sh", "f() { echo $#:$1:$2; }\nf a b c\nf\n", "", "3:a:b\n0::\n", 0);
    expect("fn3.sh", "f() { return 3; echo no; }\nf\necho $?\n", "", "3\n", 0);
    expect("fn4.sh", "f() { expr 1 = 2; }\nf\necho $?\n", "", "0\n1\n", 0);
    expect("fn5.sh", "f() { echo in; return; }\nexpr 1 = 2\nf\necho $?\n", "", "0\nin\n0\n", 0);

    // the arguments of the caller come back; $0 is the script
    expect("fn6.sh", "f() { echo $1; }\ng() { f inner; echo $1; }\ng outer\necho \"[$1]\"\n", "x", "inner\nouter\n[x]\n", 0);

    // variables are the shell's own, unless local
    expect("fn7.sh", "n=1\nf() { n=2; }\nf\necho $n\n", "", "2\n", 0);
    expect("fn8.sh", "n=1\nf() { local n=2; echo in $n; }\nf\necho out $n\n", "", "in 2\nout 1\n", 0);
    expect("fn9.sh", "f() { local u; echo \"[$u]\"; u=set; }\nf\necho \"[$u]\"\n", "", "[]\n[]\n", 0);

    // recursion, and a function in a substitution
    expect("fn10.sh", "fact() { if [ $1 -le 1 ]; then echo 1; return; fi; p=$(fact $(($1 - 1))); echo $(($1 * p)); }\n"
                      "echo $(fact 5)\n", "", "120\n", 0);
    expect("fn11.sh", "count() { if [ $1 -gt 0 ]; then echo $1; count $(($1 - 1)); fi; }\ncount 3\n", "", "3\n2\n1\n", 0);

    // return from inside loops; break in a function leaves the loops of the function
    expect("fn12.sh", "f() { for i in 1 2 3; do if [ $i -eq 2 ]; then return 9; fi; echo $i; done; }\nf\necho $?\n", "",
           "1\n9\n", 0);

    // other bodies and the layout
    expect("fn13.sh", "f()\n{\n  echo layout\n}\nf\ng () { echo spaced; }\ng\nh() if true; then echo ifbody; fi\nh\n", "",
           "layout\nspaced\nifbody\n", 0);
    expect("fn14.sh", "f() { echo one; }\nf() { echo two; }\nf\nunset -f f\nf 2> nul\necho $?\n", "", "two\n127\n", 0);

    // exit in a function ends the shell; a function can be a loop's command
    expect("fn15.sh", "f() { echo bye; exit 6; }\nf\necho never\n", "", "bye\n", 6);
    expect("fn16.sh", "show() { echo \"<$1>\"; }\nfor x in a b; do show $x; done\n", "", "<a>\n<b>\n", 0);

    // redirections and && of a call
    expect("fn17.sh", "f() { echo to-file; }\nf > fn17.txt\ncat fn17.txt\nf && echo ok\n", "", "to-file\nto-file\nok\n", 0);

    // a function body has to be a compound command; return outside of a function
    {
        RunResult r;

        write_text(workDir, "fn18.sh", "f() echo no\n");
        run_shell("fn18.sh", "", &r);
        CHECK_INT(r.exitCode, 2);
        CHECK(strstr(r.err, "compound command") != NULL);
        run_result_free(&r);

        write_text(workDir, "fn19.sh", "return 4\necho $?\n");
        run_shell("fn19.sh", "", &r);
        lf_only(r.out);
        CHECK_STR(r.out, "1\n");
        CHECK(strstr(r.err, "can only `return' from a function") != NULL);
        run_result_free(&r);
    }

    // { list; }
    expect("g1.sh", "{ echo a; echo b; }\n{ echo c\n  echo d\n} > g1.txt\ncat g1.txt\n", "", "a\nb\nc\nd\n", 0);
    expect("g2.sh", "{ expr 1 = 2; } || echo failed\ntrue && { echo yes; echo yes2; }\n", "", "0\nfailed\nyes\nyes2\n", 0);
    expect("g3.sh", "echo piped | { read a; echo \"got $a\"; }\n", "", "got piped\n", 0);

    // ( list ): a subshell
    expect("s1.sh", "(echo in)\n(exit 4)\necho $?\n", "", "in\n4\n", 0);
    expect("s2.sh", "v=1\n(v=2; echo $v)\necho $v\n", "", "2\n1\n", 0);
    expect("s3.sh", "cd /c\n(cd /c/Users; pwd)\npwd\n", "", "/c/Users\n/c\n", 0);
    expect("s4.sh", "f() { echo $1; }\n(f sub)\n(echo a; echo b) > s4.txt\ncat s4.txt\n", "", "sub\na\nb\n", 0);
    expect("s5.sh", "(echo a; (echo b; (echo c)))\n", "", "a\nb\nc\n", 0);
}

static void test_trap(void) {
    printf("trap\n");
    // EXIT: at the end of the script, and with exit; the status of the shell is kept
    expect("t1.sh", "trap 'echo bye' EXIT\necho hi\n", "", "hi\nbye\n", 0);
    expect("t2.sh", "trap 'echo bye $?' EXIT\nexit 3\n", "", "bye 3\n", 3);
    expect("t3.sh", "trap 'echo bye; exit 9' EXIT\nexit 3\n", "", "bye\n", 9);
    expect("t4.sh", "trap 'echo one' EXIT\ntrap 'echo two' EXIT\n", "", "two\n", 0);
    expect("t5.sh", "trap 'echo gone' EXIT\ntrap - EXIT\necho x\n", "", "x\n", 0);
    expect("t6.sh", "trap 'echo gone' 0\n", "", "gone\n", 0);
    // listing
    expect("t7.sh", "trap 'echo it'\"'\"'s' EXIT\ntrap\ntrap -p EXIT\ntrap - EXIT\ntrap\n", "",
           "trap -- 'echo it'\\''s' EXIT\ntrap -- 'echo it'\\''s' EXIT\n", 0);
    // ERR: after a failing command, not where the status is tested
    expect("t8.sh", "trap 'echo err $?' ERR\nfalse\ntrue\nexpr 1 = 2\n", "", "err 1\n0\nerr 1\n", 1);
    expect("t9.sh", "trap 'echo err' ERR\nfalse || true\nif false; then :; fi\nwhile false; do :; done\nfalse && true\necho end\n",
           "", "end\n", 0);
    // a function does not inherit it, its call that fails does
    expect("t10.sh", "trap 'echo err' ERR\nf() { false; true; }\nf\ng() { return 3; }\ng\n", "", "err\n", 3);
    // a subshell and a substitution start without the traps
    expect("t11.sh", "trap 'echo outer' EXIT\n(echo in; exit 0)\necho \"$(echo sub)\"\n", "", "in\nsub\nouter\n", 0);
    expect("t12.sh", "trap 'echo outer' EXIT\n(trap 'echo inner' ERR; false)\necho \"[$(trap)]\"\n", "", "inner\n[]\nouter\n", 0);
    // INT can be ignored or set; the other signals are refused
    expect("t13.sh", "trap '' INT\ntrap\ntrap - INT\ntrap 'echo h' 2\ntrap -p\n", "",
           "trap -- '' INT\ntrap -- 'echo h' INT\n", 0);
    {
        RunResult r;

        write_text(workDir, "t14.sh", "trap 'echo x' TERM\necho $?\ntrap 'echo x' BOGUS\necho $?\n");
        run_shell("t14.sh", "", &r);
        lf_only(r.out);
        CHECK_STR(r.out, "1\n1\n");
        CHECK(strstr(r.err, "TERM: signal not supported") != NULL);
        CHECK(strstr(r.err, "BOGUS: invalid signal specification") != NULL);
        run_result_free(&r);
    }
    // the trap sees the variables of the script, and $? outside is as it was
    expect("t15.sh", "n=5\ntrap 'echo n=$n' EXIT\nn=6\n", "", "n=6\n", 0);
}

static void test_compound_pipelines(void) {
    printf("functions and compound commands as stages of a pipeline\n");
    const char* up = "up() { while read l; do echo \"[$l]\"; done; }\n";
    char text[512];

    // a function at the end, in the middle and at the start
    snprintf(text, sizeof text, "%sprintf 'a\\nb\\n' | up\n", up);
    expect("p1.sh", text, "", "[a]\n[b]\n", 0);
    snprintf(text, sizeof text, "%sprintf 'x\\ny\\n' | up | tr a-z A-Z\n", up);
    expect("p2.sh", text, "", "[X]\n[Y]\n", 0);
    snprintf(text, sizeof text, "%sgen() { echo one; echo two; }\ngen | grep tw | up\n", up);
    expect("p3.sh", text, "", "[two]\n", 0);

    // a loop, a group, a subshell and an if as stages; a redirection before the pipe
    snprintf(text, sizeof text, "%sfor i in 3 1 2; do echo $i; done | sort | up\n", up);
    expect("p4.sh", text, "", "[1]\n[2]\n[3]\n", 0);
    snprintf(text, sizeof text, "%s{ echo g1; echo g2; } | up\n(echo s1; echo s2) | up\n", up);
    expect("p5.sh", text, "", "[g1]\n[g2]\n[s1]\n[s2]\n", 0);
    snprintf(text, sizeof text, "%sprintf 'l1\\nl2\\n' > in.txt\nwhile read x; do echo \"got $x\"; done < in.txt | up\n", up);
    expect("p6.sh", text, "", "[got l1]\n[got l2]\n", 0);
    expect("p7.sh", "echo hello | if read x; then echo \"read $x\"; fi\n", "", "read hello\n", 0);
    expect("p8.sh", "printf 'a\\nb\\n' | while read x; do echo \"<$x>\"; done | tr a-z A-Z\n", "", "<A>\n<B>\n", 0);

    // the status is that of the last stage; the stages but the last run in subshells
    expect("p9.sh", "f() { cat > nul; return 3; }\necho x | f\necho $?\necho x | f | cat\necho $?\n", "", "3\n0\n", 0);
    expect("p10.sh", "(echo q; exit 5) | cat\nv=1\n(v=2; echo $v) | cat\necho $v\n", "", "q\n2\n1\n", 0);
    // the last stage runs in the shell itself: its assignments stay, and a builtin can be a stage
    expect("p11.sh", "echo val | { read v; }\necho \"v=$v\"\necho val2 | read w\necho \"w=$w\"\n", "", "v=val\nw=val2\n", 0);
    // arguments, quotes and a function that is called with the input of the previous stage
    expect("p12.sh", "tag() { while read l; do echo \"$1:$l\"; done; }\necho z | tag T\necho 'a|b' | tag Q\n", "", "T:z\nQ:a|b\n", 0);
    // a pipe at the end of a line goes on in the next one
    expect("p13.sh", "for i in 1 2; do echo $i; done |\n  tr 12 ab\n", "", "a\nb\n", 0);
}

static void test_background(void) {
    printf("background jobs\n");
    // a program: $! is its process id, wait waits for it
    expect("b1.sh", "sleep 1 &\nwait\necho done\n", "", "done\n", 0);
    expect("b2.sh", "echo bg > b2.txt &\nwait $!\ncat b2.txt\n", "", "bg\n", 0);
    expect("b3.sh", "sleep 1 &\ntest -n \"$!\" && echo has-pid\nwait\n", "", "has-pid\n", 0);
    // the status of the command with & is 0, wait gives the status of the job
    expect("b4.sh", "(sleep 1; exit 3) &\necho \"started $?\"\nwait $!\necho \"waited $?\"\n", "", "started 0\nwaited 3\n", 0);
    // only the last command of the line goes to the background; an and-or list goes as a whole
    expect("b5.sh", "echo first; echo second &\nwait\n", "", "first\nsecond\n", 0);
    expect("b6.sh", "sleep 1 && echo and-list &\nwait\n", "", "and-list\n", 0);
    // functions, loops and groups run in a shell of their own that is given the variables and functions
    expect("b7.sh", "n=5\nf() { echo \"f $n $1\"; }\nf hello &\nwait\n", "", "f 5 hello\n", 0);
    expect("b8.sh", "for i in 1 2; do echo \"loop $i\"; done &\nwait\n", "", "loop 1\nloop 2\n", 0);
    expect("b9.sh", "{ sleep 1; echo group; } &\necho not-waiting\nwait\n", "", "not-waiting\ngroup\n", 0);
    expect("b10.sh", "set_x() { echo \"$1:$2\"; }\nset_x a b &\nwait\necho x | cat &\nwait\n", "", "a:b\nx\n", 0);
    // the input of a job is empty, jobs lists the running ones, and a finished one is gone after wait
    expect("b11.sh", "cat &\nwait\necho end\n", "", "end\n", 0);
    expect("b12.sh", "sleep 2 &\njobs\nwait\njobs\necho end\n", "", "[1]+  Running               sleep 2 &\nend\n", 0);
    // wait for a process that is not a child, and for a job number
    {
        RunResult r;

        write_text(workDir, "b13.sh", "wait 99999\necho $?\nsleep 1 &\nwait %1\necho $?\nwait %7\necho $?\n");
        run_shell("b13.sh", "", &r);
        lf_only(r.out);
        CHECK_STR(r.out, "127\n0\n127\n");
        CHECK(strstr(r.err, "pid 99999 is not a child of this shell") != NULL);
        run_result_free(&r);
    }
    // fg waits for a job and gives its status; kill takes job specs; bg goes on with a stopped job
    expect("b20.sh", "sleep 1 &\nfg\necho \"s=$?\"\n", "", "sleep 1\ns=0\n", 0);
    expect("b21.sh", "(sleep 1; exit 4) &\nfg %1\necho $?\n", "", "(sleep 1; exit 4)\n4\n", 0);
    expect("b22.sh", "sleep 30 &\nsleep 40 &\nkill %1\nwait %1\necho $?\nkill -9 %2\nwait %2\necho $?\njobs\n", "", "143\n137\n", 0);
    expect("b23.sh", "sleep 30 &\nkill %%\nwait\necho $?\nsleep 30 &\nsleep 30 &\nkill %- %+\nwait\necho $?\n", "", "0\n0\n", 0);
    expect("b24.sh", "sleep 30 &\nkill -STOP %1\njobs\nbg\njobs\nbg\nkill %1\nwait\n", "",
           "[1]+  Stopped               sleep 30 &\n[1]+ sleep 30 &\n[1]+  Running               sleep 30 &\n", 0);
    expect("b25.sh", "sleep 2 &\nsleep 2 &\njobs -p | wc -l\ndisown %1\njobs -p | wc -l\ndisown -a\njobs\necho end\n", "", "2\n1\nend\n", 0);
    expect("b26.sh", "sleep 30 &\nkill %sl\nwait\nsleep 30 &\nkill %?eep\nwait\necho ok\n", "", "ok\n", 0);
    {
        RunResult r;

        write_text(workDir, "b27.sh", "kill %9\necho $?\nfg %5\necho $?\nfg\necho $?\nbg\necho $?\nsleep 5 &\nbg\necho $?\nkill %1\n");
        run_shell("b27.sh", "", &r);
        lf_only(r.out);
        CHECK_STR(r.out, "1\n1\n1\n1\n0\n");
        CHECK(strstr(r.err, "kill: %9: no such job") != NULL);
        CHECK(strstr(r.err, "fg: %5: no such job") != NULL);
        CHECK(strstr(r.err, "bg: job 1 already in background") != NULL);
        run_result_free(&r);
    }
    // $$ is the process id of the shell, not the same as $! of a child
    expect("b14.sh", "echo $$ | grep -c '^[0-9][0-9]*$'\n", "", "1\n", 0);
    // redirections with & in them are not a job
    expect("b15.sh", "echo err 2>&1 | cat\necho both &> b15.txt\ncat b15.txt\n", "", "err\nboth\n", 0);
    // a & with no command
    {
        RunResult r;

        write_text(workDir, "b16.sh", "& echo x\n");
        run_shell("b16.sh", "", &r);
        CHECK_INT(r.exitCode, 2);
        run_result_free(&r);
    }
}

static void test_positionals(void) {
    const char* show = "show() { echo \"n=$#\"; for a in \"$@\"; do echo \"[$a]\"; done; }\n";
    char text[512];

    printf("\"$@\" and the positional parameters\n");
    // every parameter is a word, whatever it contains
    snprintf(text, sizeof text, "%sshow a \"b c\" \"\" d\n", show);
    expect("a1.sh", text, "", "n=4\n[a]\n[b c]\n[]\n[d]\n", 0);
    snprintf(text, sizeof text, "%sshow\nshow \"\"\n", show);
    expect("a2.sh", text, "", "n=0\nn=1\n[]\n", 0);
    // "$*" is one word, $@ without quotes is split at blanks
    expect("a3.sh", "f() { for a in \"$*\"; do echo \"<$a>\"; done; for a in $@; do echo \"[$a]\"; done; }\nf a \"b c\"\n", "",
           "<a b c>\n[a]\n[b]\n[c]\n", 0);
    // text around "$@" joins the first and the last
    expect("a4.sh", "f() { for a in \"x$@y\" \"${@}\"; do echo \"[$a]\"; done; }\nf 1 2 3\nf\n", "",
           "[x1]\n[2]\n[3y]\n[1]\n[2]\n[3]\n[xy]\n", 0);
    // passed on to another function, wildcards stay plain
    expect("a5.sh", "inner() { echo \"$# $1|$2\"; }\nouter() { inner \"$@\"; }\nouter \"one two\" '*'\n", "", "2 one two|*\n", 0);
    // more than nine, shift
    expect("a6.sh", "m() { echo \"$# ${10} ${12}\"; shift 2; echo \"$# $1 $@\"; shift 10; echo \"status $?\"; }\nm a b c d e f g h i j k l\n", "",
           "12 j l\n10 c c d e f g h i j k l\nstatus 0\n", 0);
    expect("a7.sh", "f() { shift; echo \"status $?\"; }\nf\n", "", "status 1\n", 0);
    // `for x; do` goes over the parameters
    expect("a8.sh", "f() { for x; do echo \"<$x>\"; done; }\nf \"a b\" c\n", "", "<a b>\n<c>\n", 0);
    // a subshell and a substitution do not change the parameters of the caller
    expect("a9.sh", "f() { (shift; echo \"in $#\"); x=$(shift; echo $#); echo \"out $# $x\"; }\nf 1 2 3\n", "", "in 2\nout 3 2\n", 0);
    // the arguments of a script and of -c
    expect("a10.sh", "for a in \"$@\"; do echo \"[$a]\"; done\necho \"$#\"\n", "\"p q\" r \"\"", "[p q]\n[r]\n[]\n3\n", 0);
}

static void test_braces(void) {
    printf("brace expansion in scripts\n");
    expect("c1.sh", "echo a{b,c}d {1..3} {a..c}{1,2}\n", "", "abd acd 1 2 3 a1 a2 b1 b2 c1 c2\n", 0);
    expect("c2.sh", "for i in {1..3}; do echo \"i=$i\"; done\n", "", "i=1\ni=2\ni=3\n", 0);
    expect("c3.sh", "mkdir -p bd{1,2}/sub\nls -d bd?/sub\nrm -r bd1 bd2\n", "", "bd1/sub\nbd2/sub\n", 0);
    expect("c4.sh", "echo $(echo {p,q}) \"{a,b}\" '{c,d}' {x}\n", "", "p q {a,b} {c,d} {x}\n", 0);
    expect("c5.sh", "v=x\necho ${v}{1,2}\na={m,n}\necho \"$a\"\n", "", "x1 x2\n{m,n}\n", 0);
    expect("c6.sh", "f() { for a in \"$@\"; do echo \"[$a]\"; done; }\nf {1,2} \"{3,4}\"\n", "", "[1]\n[2]\n[{3,4}]\n", 0);
    expect("c7.sh", "case b in {a,b}) echo literal;; b) echo plain;; esac\n", "", "plain\n", 0);
}

static void test_replace(void) {
    printf("${NAME/pattern/string}\n");
    expect("r1.sh", "v=\"hello world, hello\"\necho \"${v/hello/bye}\"\necho \"${v//hello/bye}\"\necho \"${v/o/}\" \"${v//o}\"\n", "",
           "bye world, hello\nbye world, bye\nhell world, hello hell wrld, hell\n", 0);
    // anchored at the start or at the end
    expect("r2.sh", "v=\"hello world, hello\"\necho \"${v/#hello/X}\" \"${v/#world/X}\" \"${v/%hello/Z}\" \"${v/%world/Z}\"\n", "",
           "X world, hello hello world, hello hello world, Z hello world, hello\n", 0);
    // wildcards: the longest match that starts first
    expect("r3.sh", "v=\"hello world, hello\"\necho \"${v/h*o/_}\" \"${v//[aeiou]/*}\"\nn=1234\necho \"${n/2*/_}\"\n", "",
           "_ h*ll* w*rld, h*ll*\n1_\n", 0);
    // variables in the pattern and in the string; operators and blanks are plain text
    expect("r4.sh", "v=\"hello world, hello\"\np=hello\nr=ZZ\necho \"${v/world/$r}\" \"${v/$p/<$p>}\"\nf=/usr/local/bin\necho \"${f//\\//|}\"\n",
           "", "hello ZZ, hello <hello> world, hello\n|usr|local|bin\n", 0);
    expect("r5.sh", "a=\"a b c\"\necho ${a// /,}\nfor w in ${a// /,}; do echo \"[$w]\"; done\necho \"${a/ /_}\"\n", "",
           "a,b,c\n[a,b,c]\na_b c\n", 0);
    // nothing to replace, an empty pattern, an unset variable, UTF-8 text
    expect("r6.sh", "x=aaa\necho \"${x/b/c}\" \"${x/}\" \"${x//}\" \"${nothing/a/b}|\"\nk=\"가나다라\"\necho \"${k/나/N}\"\n", "",
           "aaa aaa aaa |\n가N다라\n", 0);
    expect("r7.sh", "f=report.txt\necho \"${f/.txt/.md}\" \"${f/%.txt/.bak}\" \"${f/#rep/REP}\" ${f//t/T}\n", "",
           "report.md report.bak REPort.txt repor" "T.TxT\n", 0);
    // quotes in the pattern; the first } ends the expansion, as in bash
    expect("r8.sh", "v=\"{a,b}\"\necho \"${v/'{a,b}'/X}\" \"${v/{a,b}/X}\"\n", "", "X }/X}\n", 0);
}

int main(void) {
    tools_init("shell_control_test");
    test_replace();
    test_braces();
    test_positionals();
    test_compound_pipelines();
    test_background();
    test_if();
    test_loops();
    test_break_continue();
    test_input();
    test_case();
    test_functions();
    test_trap();
    test_syntax();
    return test_summary("shell_control_test");
}
