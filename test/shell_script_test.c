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

    // the options as bash reads them, standard input as a script without a prompt, programs found in PATH
    expect_c("-lc \"echo login\"", "login\n", 0);
    expect_c("-ec \"echo a; false; echo b\"", "a\n", 1);
    expect_c("-euo pipefail -c \"false | true; echo no\"", "", 1);
    expect_c("--login --noprofile -c \"echo ok\"", "ok\n", 0);
    expect_c("-- -c", "", 127);
    run_shell("-s a b", "echo \"$1 $2\"\n", &r);
    CHECK_STR(r.out, "a b\n");
    run_result_free(&r);
    run_shell("", "echo one\nexit 4\n", &r);
    CHECK_INT(r.exitCode, 4);
    CHECK(strchr(r.out, '>') == NULL);                       // no prompt: standard input is not a console
    run_result_free(&r);
    expect_c("-c \"cmd /c echo viapath\"", "viapath\n", 0);
    run_shell("-c", "", &r);
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

/** What a script of an agent needs: set, ! , here-strings and here documents, backslash escapes, the null device. */
static void test_agent_scripts(void) {
    printf("set\n");
    expect_script("e1.sh", "set -e\necho a\nfalse\necho b\n", "", "a\n", 1);
    expect_script("e2.sh", "set -e\nfalse || echo handled\nif false; then echo no; fi\necho end\n", "", "handled\nend\n", 0);
    expect_script("e3.sh", "set -u\necho ${x:-d}\necho $x\necho no\n", "", "d\n", 1);
    expect_script("e4.sh", "set -o pipefail\nfalse | true\necho $?\nset +o pipefail\nfalse | true\necho $?\n", "", "1\n0\n", 0);
    expect_script("e5.sh", "set -- a 'b c'\necho $# $2\n", "", "2 b c\n", 0);
    expect_script("e6.sh", "! false\necho $?\n! true\necho $?\n", "", "0\n1\n", 0);
    expect_script("e7.sh", "echo x > /dev/null\ncat /dev/null\necho ok\n", "", "ok\n", 0);

    printf("here-strings and here documents\n");
    expect_script("h1.sh", "read a b <<< \"x y z\"\necho \"$a|$b\"\n", "", "x|y z\n", 0);
    expect_script("h2.sh", "cat <<EOF\nline $((1+2))\nEOF\ncat <<'EOF'\nno $x\nEOF\n", "", "line 3\nno $x\n", 0);
    expect_script("h3.sh", "cat <<-EOF\n\ttabs gone\n\tEOF\ncat <<EOF | tr a-z A-Z\nup\nEOF\n", "", "tabs gone\nUP\n", 0);
    expect_script("h4.sh", "f() { cat <<EOF\nin $1\nEOF\n}\nf arg\ncat <<A; cat <<B\none\nA\ntwo\nB\n", "", "in arg\none\ntwo\n", 0);

    printf("backslashes\n");
    expect_script("b1.sh", "echo a\\ b\necho one \\\n  two\necho \"\\$x\"\n", "", "a b\none two\n$x\n", 0);
    expect_script("b2.sh", "echo C:\\Users\\me\n", "", "C:\\Users\\me\n", 0);

    printf("[[ ]], (( )) and the builtins of bash\n");
    expect_script("d1.sh", "[[ abc == a* ]] && echo glob\n[[ abc == \"a*\" ]] || echo literal\n[[ -n x && -z \"\" ]] && echo and\n[[ 5 -gt 3 || 1 -eq 2 ]] && echo num\n", "",
                  "glob\nliteral\nand\nnum\n", 0);
    expect_script("d2.sh", "[[ foo123 =~ ([a-z]+)([0-9]+) ]] && echo \"${BASH_REMATCH[1]}-${BASH_REMATCH[2]}\"\n[[ ! -e nothere ]] && echo none\n[[ a < b ]] && echo lt\n", "",
                  "foo-123\nnone\nlt\n", 0);
    expect_script("d3.sh", "i=0\nwhile (( i < 3 )); do echo $i; (( i++ )); done\nx=5; (( x > 3 )) && echo big\ny=$((x++)); echo $y $x\nlet \"k = 2 + 3\"; echo $k\n", "",
                  "0\n1\n2\nbig\n5 6\n5\n", 0);
    expect_script("d4.sh", "eval 'a=1; b=2'\necho $a$b\nreadonly R=1\nR=2 2>/dev/null\necho $R\ncommand -v cd\ntype cd\n", "",
                  "12\n1\ncd\ncd is a shell builtin\n", 0);
    expect_script("d6.sh", "function m { echo m; }\nm\nfor ((i=0; i<3; i++)); do echo $i; done\necho $'a\\tb'\ns=Hello-World\necho ${s^^} ${s,,} ${s:2} ${s: -3} ${s:0:-2}\nn=s; echo ${!n}\n", "",
                  "m\n0\n1\n2\na\tb\nHELLO-WORLD hello-world llo-World rld Hello-Wor\nHello-World\n", 0);
    expect_script("d7.sh", "cat <(echo from_proc)\nwhile read l; do echo \"[$l]\"; done < <(printf 'x\\ny\\n')\ndiff <(echo a) <(echo a) && echo same\n", "",
                  "from_proc\n[x]\n[y]\nsame\n", 0);
    printf("what an agent does\n");
    expect_script("a1.sh", "> e.txt\nls e.txt\necho hi > /tmp/winshell_t_x\ncat /tmp/winshell_t_x\nrm /tmp/winshell_t_x\n", "", "e.txt\nhi\n", 0);
    expect_script("a2.sh", "cat > sc <<'EOF'\necho from script $1\nEOF\n./sc a\ncat > sb <<'EOF'\n#!/bin/bash\necho sb $1\nEOF\n./sb z\n", "",
                  "from script a\nsb z\n", 0);
    expect_script("a3.sh", "mkdir bin2\ncat > bin2/mytool <<'EOF'\necho mytool\nEOF\nexport PATH=\"$PWD/bin2:$PATH\"\nmytool\necho /?\n", "",
                  "mytool\n/?\n", 0);
    expect_script("a4.sh", "yes | head -2\ncmd /c echo via\n", "", "y\ny\nvia\n", 0);
    expect_script("m1.sh", "declare -A m; m[a b]=1; m[c]=2; m[c]=3\necho \"${m[a b]} ${m[c]} ${#m[@]}\"\nfor k in \"${!m[@]}\"; do echo \"$k=${m[$k]}\"; done\nunset m[c]; echo \"${m[@]}\"\n", "",
                  "1 3 2\na b=1\nc=3\n1\n", 0);
    expect_script("m2.sh", "declare -i n=3; n=n+2; echo $n\nIFS=,; set -- a b c; echo \"$*\"; unset IFS\nf() { echo \"${@:2}\"; echo \"${*:2:1}\"; }; f x y z\n", "",
                  "5\na,b,c\ny z\ny\n", 0);
    expect_script("m3.sh", "case a in a) echo 1;;& *) echo 2;; esac\ncase a in a) echo A;& b) echo B;; c) echo C;; esac\n", "",
                  "1\n2\nA\nB\n", 0);
    expect_script("m4.sh", "echo 'echo in; return 4; echo no' > r.sh\n. ./r.sh\necho $?\nexec echo replaced\necho not reached\n", "", "in\n4\nreplaced\n", 0);
    expect_script("d5.sh", "f() { while getopts \"ab:\" o; do case $o in a) echo A;; b) echo B=$OPTARG;; esac; done; }\nf -a -b val\nOPTIND=1\nf -ab x\n", "",
                  "A\nB=val\nA\nB=x\n", 0);
    // descriptors above 2, and exec with only redirections
    expect_script("x1.sh", "exec 3> fd3.txt\necho to3 >&3\n{ echo b; echo c; } >&3\nexec 3>&-\ncat fd3.txt\n", "", "to3\nb\nc\n", 0);
    expect_script("x2.sh", "echo hi > in.txt\nexec 4< in.txt\nread l <&4; echo \"[$l]\"\nexec 4<&-\necho done\n", "", "[hi]\ndone\n", 0);
    expect_script("x3.sh", "exec > out.txt 2>&1\necho one\necho two >&2\nls no_such_file_zz\n", "", "", 2);
    expect_script("x4.sh", "echo b >&3\necho status $?\n", "", "status 1\n", 0);
    expect_script("x5.sh", "echo data > x5src.txt\ncat x5src.txt 3> x5.txt >&3\ncat x5.txt\nf() { echo in_f >&3; }\nexec 3> x5b.txt; f; exec 3>&-; cat x5b.txt\n", "", "data\nin_f\n", 0);
    // shopt: dotglob, nullglob, and the byte order of the C locale
    expect_script("g1.sh", "touch a.dat B.dat .hid.dat\nLC_ALL=C; echo *.dat\nshopt -s dotglob; echo *.dat\nshopt -u dotglob\nshopt -s nullglob; echo zz*.nope; echo [A-Z]*.dat\nshopt -q nullglob && echo on\nshopt -u nullglob; echo zz*.nope\n", "",
                  "B.dat a.dat\n.hid.dat B.dat a.dat\n\nB.dat\non\nzz*.nope\n", 0);
}

int main(void) {
    tools_init("shell_script_test");
    test_agent_scripts();
    test_scripts();
    test_substitution();
    test_expansion();
    test_conditions();
    test_source();
    return test_summary("shell_script_test");
}
