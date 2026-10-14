/*
 * Tests of the shell as a script interpreter, run as the program it is: a file as the script, -c, source,
 * the parameters $0 $1.. $# $@ $*, command substitution $(...) and `...`, comments, and the exit status.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

/** Runs `shell <arguments>` in the work directory. */
static void run_shell(const char* arguments, const char* input, RunResult* r) {
    CHECK(run_program(shellPath, arguments, input, workDir, r) == 0);
    CHECK(!r->timedOut);
}

static void expect_script(const char* name, const char* script, const char* arguments, const char* expected,
                          int status) {
    RunResult r;
    char command[MAX_PATH * 2];

    write_text(workDir, name, script);
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

static void expect_c(const char* arguments, const char* expected, int status) {
    RunResult r;

    run_shell(arguments, "", &r);
    if (r.out != NULL)
        lf_only(r.out);
    if (r.out == NULL || strcmp(r.out, expected) != 0)
        printf("    for: shell %s\n    got: \"%s\"\n    err: \"%s\"\n", arguments, r.out == NULL ? "(null)" : r.out,
               r.err == NULL ? "" : r.err);
    CHECK_STR(r.out, expected);
    CHECK_INT(r.exitCode, status);
    run_result_free(&r);
}

static void test_scripts(void) {
    RunResult r;

    printf("script files\n");
    expect_script("s1.sh", "echo one\n# a comment\necho two # another\n\necho three\n", "", "one\ntwo\nthree\n", 0);
    expect_script("s2.sh", "echo $0 [$1] [$2] [$3] $#\n", "a \"b c\"", "s2.sh [a] [b c] [] 2\n", 0);
    expect_script("s3.sh", "echo $@\necho \"$*\"\necho $#\n", "x y z", "x y z\nx y z\n3\n", 0);
    expect_script("s4.sh", "echo ${1}0 $10\n", "p", "p0 p0\n", 0);

    // CR LF line ends and a byte order mark
    expect_script("s5.sh", "echo a\r\necho b\r\n", "", "a\nb\n", 0);
    expect_script("s6.sh", "\xEF\xBB\xBF" "echo bom\n", "", "bom\n", 0);

    // the status of the script is that of its last command, or of exit
    expect_script("s7.sh", "echo x\nexit 7\necho never\n", "", "x\n", 7);
    expect_script("s8.sh", "expr 1 = 2\n", "", "0\n", 1);
    expect_script("s9.sh", "exit\n", "", "", 0);

    // a command that cannot be parsed is reported and ends the script; what was before it has run
    expect_script("s10.sh", "echo before\necho 'open\necho next\n", "", "before\n", 2);

    // variables and the directory are the script's own in a script
    expect_script("s11.sh", "x=5\necho $x\ncd /c\npwd\n", "", "5\n/c\n", 0);

    printf("-c and standard input\n");
    expect_c("-c \"echo hello\"", "hello\n", 0);
    expect_c("-c \"echo $0 $1 $#\" name a b", "name a 2\n", 0);
    expect_c("-c \"echo $0\"", "shell\n", 0);
    expect_c("-c \"exit 3\"", "", 3);

    // standard input is still read as the commands
    run_shell("", "echo $(echo from input)\n", &r);
    CHECK(strstr(r.out, "from input") != NULL);
    run_result_free(&r);

    run_shell("nosuch.sh", "", &r);
    CHECK_INT(r.exitCode, 127);
    CHECK(strstr(r.err, "nosuch.sh: No such file or directory") != NULL);
    run_result_free(&r);

    run_shell("--bogus", "", &r);
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);
}

static void test_substitution(void) {
    printf("command substitution\n");
    expect_c("-c \"echo $(echo a b)\"", "a b\n", 0);
    expect_c("-c \"x=$(echo hello world); echo [$x]\"", "[hello world]\n", 0);

    // line ends at the end go, the ones inside stay (and are blanks to the word splitting)
    expect_c("-c \"echo [$(echo a)]\"", "[a]\n", 0);
    expect_c("-c \"echo \\\"[$(echo a  b)]\\\"\"", "[a b]\n", 0);

    // words: unquoted it is cut at blanks, quoted it is one
    expect_c("-c \"echo $(echo 1   2) | cat\"", "1 2\n", 0);
    expect_c("-c \"echo 'a;b' > t.txt; for_count=$(wc -l < t.txt); echo [$for_count]\"", "[1]\n", 0);
    expect_c("-c \"echo \\\"$(echo a)\\\"x $(echo b)y\"", "ax by\n", 0);

    // the old form, and nesting
    expect_c("-c \"echo `echo back`\"", "back\n", 0);
    expect_c("-c \"echo $(echo $(echo $(echo deep)))\"", "deep\n", 0);
    expect_c("-c \"echo \\\"$(echo \\\"in quotes\\\")\\\"\"", "in quotes\n", 0);

    // inside single quotes it is text; a substitution may hold operators
    expect_c("-c \"echo '$(echo no)'\"", "$(echo no)\n", 0);
    expect_c("-c \"echo $(echo a; echo b) end\"", "a b end\n", 0);
    expect_c("-c \"echo $(expr 1 = 1 && echo yes)\"", "1 yes\n", 0);

    // the status: of the substitution for x=$(...) alone, and $? around it is not changed
    expect_c("-c \"x=$(expr 1 = 2); echo $?\"", "1\n", 0);
    expect_c("-c \"expr 1 = 2; echo $(expr 1 = 1) $?\"", "0\n1 1\n", 0);
    expect_c("-c \"x=$(echo ok); echo $?\"", "0\n", 0);

    // a subshell: the directory, the variables and exit stay inside
    expect_c("-c \"cd /c; x=$(cd /c/Users; pwd); pwd; echo $x\"", "/c\n/c/Users\n", 0);
    expect_c("-c \"v=1; echo $(v=2; echo $v) $v\"", "2 1\n", 0);
    expect_c("-c \"echo $(exit 5; echo no) after\"", "after\n", 0);
    expect_c("-c \"export E=1; echo $(E=2; export F=3; echo $E) $E [$F]\"", "2 1 []\n", 0);

    // errors
    expect_c("-c \"echo $(echo a\"", "", 2);
    expect_c("-c \"echo $()\"", "\n", 0);
}

static void test_conditions(void) {
    printf("test, [ and && ||\n");
    write_text(workDir, "present.txt", "x\n");

    // [ is a program like any other: its ] is an argument, and a lone [ is no wildcard
    expect_script("c1.sh", "[ -f present.txt ] && echo yes || echo no\n[ -f absent.txt ] && echo yes || echo no\n", "",
                  "yes\nno\n", 0);
    expect_script("c2.sh", "x=abc\n[ \"$x\" = abc ] && echo same\n[ \"$x\" != abc ] || echo different-not\n", "",
                  "same\ndifferent-not\n", 0);
    expect_script("c3.sh", "test 3 -gt 2 && echo bigger\ntest -z \"$(echo)\" && echo empty\n", "", "bigger\nempty\n", 0);
    expect_script("c4.sh", "n=$(echo 7)\n[ $n -eq 7 ] && echo seven\n[ $# -eq 2 ] && echo two\n", "a b", "seven\ntwo\n", 0);
    expect_script("c5.sh", "true && echo t\nfalse || echo f\nfalse\necho $?\ntrue\necho $?\n", "", "t\nf\n1\n0\n", 0);
    expect_script("c6.sh", "[ -d /c ]\necho $?\n[ 1 -eq x ] 2> nul\necho $?\n", "", "0\n2\n", 0);
}

static void test_expansion(void) {
    RunResult r;

    printf("arithmetic expansion\n");
    expect_c("-c \"echo $((1+2)) $((2*3+4)) $((10/3)) $((10%3)) $((2**10)) $((-5+2))\"", "3 10 3 1 1024 -3\n", 0);
    expect_c("-c \"echo $(( (1+2)*3 )) $((7-2-1)) $((100/10/5)) $((1<<4)) $((0xff)) $((010))\"", "9 4 2 16 255 8\n", 0);
    expect_c("-c \"echo $((5>3)) $((5<3)) $((3==3)) $((3!=3)) $((2<=2)) $((1&&0)) $((1||0)) $((!5)) $((~0))\"",
             "1 0 1 0 1 0 1 0 -1\n", 0);
    expect_c("-c \"echo $((6&3)) $((6|3)) $((6^3)) $((1?10:20)) $((0?10:20))\"", "2 7 5 10 20\n", 0);

    // variables, by name or by $; assignment operators change them; the empty one is 0
    expect_c("-c \"x=7; echo $((x*2)) $(($x+1)) $((y)) $((x+=3)) $x $((z=x*2, z+1)) $z\"", "14 8 0 10 10 21 20\n", 0);
    expect_c("-c \"n=3; echo [$((n+1))] \\\"$((n*n))\\\"\"", "[4] 9\n", 0);
    expect_c("-c \"a=b+1; b=4; echo $((a))\"", "5\n", 0);

    // the branch that is not taken has no effect (and no division by zero)
    expect_c("-c \"echo $((0 && 1/0)) $((1 || 1/0)) $((1 ? 5 : 1/0))\"", "0 1 5\n", 0);

    // errors: reported, the command does not run, the shell goes on
    run_shell("-c \"echo $((1/0)); echo next\"", "", &r);
    lf_only(r.out);
    CHECK_STR(r.out, "next\n");
    CHECK(strstr(r.err, "division by 0") != NULL);
    run_result_free(&r);
    run_shell("-c \"echo $((1+)); echo next\"", "", &r);
    lf_only(r.out);
    CHECK_STR(r.out, "next\n");
    CHECK(r.err[0] != '\0');
    run_result_free(&r);

    printf("parameter expansion\n");
    expect_c("-c \"x=set; e=; echo ${x:-d} ${u:-d} ${e:-d} ${e-d}end ${u-d}\"", "set d d end d\n", 0);
    expect_c("-c \"x=set; e=; echo ${x:+alt} ${u:+alt}end ${e:+alt}end ${e+alt}\"", "alt end end alt\n", 0);
    expect_c("-c \"echo ${v:=assigned} $v ${v:=other} ${w=x} $w\"", "assigned assigned assigned x x\n", 0);
    expect_c("-c \"s=hello; echo ${#s} ${#nosuch}\"", "5 0\n", 0);
    expect_c("-c \"p=/a/b/file.tar.gz; echo ${p#*/} ${p##*/} ${p%.*} ${p%%.*} ${p%/*} ${p%%/*}end\"",
             "a/b/file.tar.gz file.tar.gz /a/b/file.tar /a/b/file /a/b end\n", 0);
    expect_c("-c \"s=abcdef; echo ${s:2} ${s:1:3} ${s: -2} ${s:4:10}end ${s:9}end\"", "cdef bcd ef efend end\n", 0);
    expect_c("-c \"f=photo.jpg; echo ${f%.jpg}.png ${f#photo}\"", "photo.png .jpg\n", 0);

    // the word is expanded; quotes keep a default in one piece
    expect_c("-c \"y=Y; echo ${u:-$y} ${u:-$(echo cmd)} ${u:-$((1+1))}\"", "Y cmd 2\n", 0);
    expect_c("-c \"for w in \\\"${u:-a b}\\\"; do echo [$w]; done\"", "[a b]\n", 0);
    expect_c("-c \"for w in ${u:-a b}; do echo [$w]; done\"", "[a]\n[b]\n", 0);

    // a ; inside the braces is not a separator
    expect_c("-c \"echo ${u:-a;b}; echo c\"", "a;b\nc\n", 0);

    // ${NAME:?word}: an error, the command does not run
    run_shell("-c \"echo ${u:?must be set}; echo next\"", "", &r);
    lf_only(r.out);
    CHECK_STR(r.out, "next\n");
    CHECK(strstr(r.err, "u: must be set") != NULL);
    run_result_free(&r);
}

static void test_source(void) {
    printf("source\n");
    write_text(workDir, "lib.sh", "echo lib $1 $#\nv=from_lib\n");
    expect_script("src1.sh", "source lib.sh a b\necho $v\n", "", "lib a 2\nfrom_lib\n", 0);
    expect_script("src2.sh", ". lib.sh\necho $v\n", "", "lib 0\nfrom_lib\n", 0);

    // without arguments it sees the parameters of the caller; with them the caller's come back
    expect_script("src3.sh", "source lib.sh\nsource lib.sh x\necho $1\n", "p", "lib p 1\nlib x 1\np\n", 0);

    write_text(workDir, "quit.sh", "echo before\nexit 4\necho after\n");
    expect_script("src4.sh", "echo start\nsource quit.sh\necho never\n", "", "start\nbefore\n", 4);

    {
        RunResult r;
        write_text(workDir, "src5.sh", "source nosuch.sh\necho $?\nsource\necho $?\n");
        run_shell("src5.sh", "", &r);
        CHECK_STR(r.out, "127\n2\n");
        CHECK(strstr(r.err, "nosuch.sh: No such file or directory") != NULL);
        run_result_free(&r);
    }

    // a script in a directory below, run by a virtual path
    make_directory("sub");
    write_text(workDir, "sub\\in.sh", "echo $0\n");
    expect_c("sub/in.sh", "sub/in.sh\n", 0);
}

int main(void) {
    tools_init("shell_script_test");
    test_scripts();
    test_substitution();
    test_expansion();
    test_conditions();
    test_source();
    return test_summary("shell_script_test");
}
