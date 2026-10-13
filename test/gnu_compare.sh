#!/bin/bash
#
# Compares head, tail, wc, sort, uniq, cut, tr, cat, tac, nl, paste, tee and grep with the GNU tools: the output bytes and the
# exit codes of many combinations of options and inputs have to be the same.
#
# The GNU tools are the ones of MSYS2 or Git for Windows (an environment with bash). They are
# run with LC_ALL=C, and so is the order of sort of this project (grep: LC_ALL=C.UTF-8).
#
#   bash test/gnu_compare.sh                     from the repository, after nmake
#   GNU_DIR=/usr/bin BIN_DIR=bin bash test/gnu_compare.sh
#   GNU_ONLY=grep bash test/gnu_compare.sh       one section: head tail wc sort uniq cut tr cat tac nl paste tee grep
#
# This script only collects the combinations. They are run by test\out\gnu_batch.exe (nmake gnu builds
# it), which starts the programs natively and several at a time: starting a program from the MSYS
# bash costs about three times as much. Without that program (or with GNU_IMMEDIATE=1) every
# combination is run here, one after the other.
#
# The exit code is the number of combinations that differ, or 0. What is meant to be different is not
# in the comparisons: a line ending of CR LF is not part of a line here (so inputs with CR LF are
# only compared for the tools whose result does not depend on that), sort -z, -h, -M and -V are
# refused, and a character is a character of UTF-8 (the MSYS locale counts bytes). tac -r and the p
# style of nl need regular expressions, which are not supported.

GNU_DIR=${GNU_DIR:-/usr/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
BIN_DIR=${BIN_DIR:-$HERE/../bin}
export LC_ALL=C

if [ ! -x "$GNU_DIR/sort.exe" ] && [ ! -x "$GNU_DIR/sort" ]; then
    echo "no GNU tools in $GNU_DIR (set GNU_DIR)"
    exit 127
fi

BATCH_EXE=${GNU_BATCH:-$HERE/out/gnu_batch.exe}
BATCH=
if [ -x "$BATCH_EXE" ] && [ -z "$GNU_IMMEDIATE" ]; then
    BATCH=1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK" || exit 127

# ---- inputs -----------------------------------------------------------------------------
printf 'banana\nApple\ncherry\napple\nBanana\n  indented\ncherry\n\nzebra\nApple pie\n10 ten\n9 nine\n-3 neg\n' > words.txt
printf '10\n9\n-3\n2.5\n-0.5\n007\n1e3\n  42\nabc\n0\n-0\n100\n2.50\n' > nums.txt
printf 'bob:30:sales\nalice:25:dev\ncarol:35:dev\ndave:25:ops\neve:30:sales\nfrank::dev\n' > table.txt
printf 'x\ty\tz\n1\t2\t3\nnodelim\n\ta\t\n' > tabs.txt
printf 'a\na\nb\nB\nb\nc\nc\nc\nd\n' > dup.txt
printf 'f1 x\nf2 x\nf3 y\nf4 y\nf5 y\nf6 z\n' > fields.txt
printf 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\nl13\n' > lines.txt
printf '' > empty.txt
printf 'no newline at all' > nonl.txt
printf '\n\n\n' > blank.txt
seq 1 20000 | sed 's/^/line /;s/$/ with some words/' > big.txt

printf 'a
\:\:\:
h1
h2
\:\:
b1



b2
\:
f1
\:\:\:
h3
\:\:
b3
' > sections.txt

TEXTS="words.txt nums.txt table.txt tabs.txt dup.txt fields.txt lines.txt empty.txt nonl.txt blank.txt big.txt"

want() { [ -z "$GNU_ONLY" ] || [ "$GNU_ONLY" = "$1" ]; }
section() { [ -n "$BATCH" ] || echo "== $1"; }

# ---- comparing ----------------------------------------------------------------------------
total=0
bad=0
CASES=cases.bin

gnu() { local t=$1; shift; if [ -x "$GNU_DIR/$t.exe" ]; then "$GNU_DIR/$t.exe" "$@"; else "$GNU_DIR/$t" "$@"; fi; }
ours() { local t=$1; shift; "$BIN_DIR/$t.exe" "$@"; }

# add_case TYPE LOCALE STDIN TOOL ARGS... : one combination. TYPE is out (output and exit code) or
# status (only the exit code); LOCALE C or U (UTF-8); STDIN a file, or - for none.
add_case() {
    local type=$1 locale=$2 input=$3 tool=$4
    shift 4
    total=$((total + 1))

    if [ -n "$BATCH" ]; then
        printf '%s\0' "$type" "$locale" "$input" "$tool" "$#" "$@" >> "$CASES"
        return
    fi

    local lc=C erc grc
    [ "$locale" = U ] && lc=C.UTF-8
    [ "$input" = - ] && input=/dev/null
    LC_ALL=$lc gnu "$tool" "$@" > exp.out 2> exp.err < "$input"; erc=$?
    LC_ALL=$lc ours "$tool" "$@" > got.out 2> got.err < "$input"; grc=$?
    if [ $erc != $grc ] || { [ "$type" = out ] && ! cmp -s exp.out got.out; }; then
        bad=$((bad + 1))
        echo "DIFFERS: $tool $* (status $erc, ours $grc)"
    fi
}

# compare TOOL ARGS... : the same arguments for both, the output and the status of both
compare() { add_case out C - "$@"; }

# compare_stdin FILE TOOL ARGS... : the same with the file as standard input
compare_stdin() { add_case out C "$@"; }

# compare_grep ARGS... : grep in a UTF-8 locale for the GNU tool: a character is a character, as it is here
compare_grep() { add_case out U - grep "$@"; }

# compare_words TOOL 'ARGS' [FILE] : the arguments as shell words (quotes and escapes are processed
# here), FILE as standard input; status_words compares only the exit codes
compare_words() {
    local tool=$1 words=$2 input=${3:--}
    eval "set -- $words"
    add_case out C "$input" "$tool" "$@"
}
status_words() {
    local tool=$1 words=$2 input=${3:--}
    eval "set -- $words"
    add_case status C "$input" "$tool" "$@"
}

# compare_now TOOL ARGS... : run at once, whatever the mode (for a tool that writes files)
compare_now() {
    local tool=$1 erc grc
    shift
    total=$((total + 1))
    gnu "$tool" "$@" > exp.out 2> exp.err < /dev/null; erc=$?
    ours "$tool" "$@" > got.out 2> got.err < /dev/null; grc=$?
    if ! cmp -s exp.out got.out || [ $erc != $grc ]; then
        bad=$((bad + 1))
        echo "DIFFERS: $tool $* (status $erc, ours $grc)"
    fi
}

if want head; then
section head
for f in $TEXTS; do
    for o in "" "-n 3" "-n 0" "-n 100" "-n -2" "-n -100" "-c 7" "-c -5" "-c 0" "-n 1k" "-3" "-c 1k"; do
        compare head $o $f
    done
done
compare head lines.txt words.txt
compare head -n 2 lines.txt words.txt nonl.txt
compare head -q -n 2 lines.txt words.txt
compare head -v -n 2 lines.txt
compare head nosuch.txt
compare head -n x lines.txt
fi

if want tail; then
section tail
for f in $TEXTS; do
    for o in "" "-n 3" "-n 0" "-n 100" "-n +3" "-n +1" "-n +100" "-c 7" "-c -5" "-c +4" "-c +1000" "-c 3b" "-3"; do
        compare tail $o $f
    done
done
compare tail -n 2 lines.txt words.txt
compare tail -q -n 2 lines.txt words.txt
compare tail -v -n 2 lines.txt
compare tail nosuch.txt
fi

if want wc; then
section wc
for f in $TEXTS; do
    for o in "" -l -w -c -m -L -lw -lwc -lwcmL -cl; do
        compare wc $o $f
    done
done
compare wc lines.txt words.txt
compare wc -l lines.txt words.txt nonl.txt
compare wc -c lines.txt big.txt
compare wc nosuch.txt lines.txt
compare_stdin lines.txt wc
compare_stdin lines.txt wc -l
fi

if want sort; then
section sort
for f in $TEXTS; do
    for o in "" -r -u -f -b -d -n -rn -nu -s -fu "-k2" "-k1,1" "-k2,2n" "-k2,2nr" "-k1.2" "-k1.2,1.3" \
             "-t: -k2n" "-t: -k2,2n -k1,1r" "-t: -k3 -k1" "-ru" "-c" "-C" "-g" "-i" "-t: -u -k3,3" "-k2b" "-k 2,2 -s"; do
        compare sort $o $f
    done
done
compare sort words.txt nums.txt
compare_now sort -o out_file.txt words.txt
compare sort nosuch.txt
compare sort -k0 words.txt
fi

if want uniq; then
section uniq
for f in dup.txt words.txt fields.txt empty.txt nonl.txt blank.txt; do
    for o in "" -c -d -D -u -i -ci "-f1" "-f 1 -c" "-s 2" "-w 1" "-w 2 -c" "-d -c" "-u -c" "-i -d"; do
        compare uniq $o $f
    done
done
compare_stdin dup.txt uniq -c
fi

if want cut; then
section cut
for f in table.txt tabs.txt words.txt empty.txt nonl.txt big.txt; do
    for o in "-c1" "-c1-3" "-c2-" "-c-2" "-c1,3" "-c3,1" "-b1-2,4" "-b 2-3 --complement" "-f1" "-f2" "-f1,3" "-f2-" \
             "-f-2" "-f1 -s" "-f2 --complement" "-d: -f1" "-d: -f2,3" "-d: -f2 -s" "-d: -f1,3 --output-delimiter=@" \
             "-d: -f 1- --output-delimiter=" "-c1-2,4-5 --output-delimiter=|" "-b1,2 --output-delimiter=-"; do
        compare cut $o $f
    done
done
compare cut -f1 nosuch.txt
compare cut words.txt
compare cut -f0 table.txt
compare cut -c1 -f1 table.txt
compare cut -f1 -d:: table.txt
fi

if want tr; then
section tr
for args in "a-z A-Z" "[:lower:] [:upper:]" "[:upper:] [:lower:]" "abc xyz" "abc x" "a-c 12" "-d a-c" "-d [:digit:]" \
            "-d [:space:]" "-s a-z" "-s ' '" "-c a-z _" "-cd a-z" "-cs a-z \\n" "-ds a-c d" "-s a-b A-B" \
            "-t abcd xy" "a-c [x*]" "abc [x*2]" "\\n \\t" "[=a=] b" "a-b -" "-d \\n"; do
    for f in words.txt table.txt; do
        compare_words tr "$args" $f
    done
done
# the errors: only the status is compared
for args in "" "a" "-d a b" "z-a x" "[:foo:] x" "a-c [x*] y" "[a*] b"; do
    status_words tr "$args" words.txt
done
fi

if want cat; then
section cat
for f in $TEXTS; do
    for o in "" -n -b -s -E -T -v -A -e -t -ns -bs -nE; do
        compare cat $o $f
    done
done
compare cat nosuch.txt lines.txt
compare cat -n lines.txt words.txt
fi

if want tac; then
section tac
for f in $TEXTS sections.txt; do
    for o in "" -b "-s :" "-b -s :" "-s ab" "-bs ab" "-s ''"; do
        compare_words tac "$o $f"
    done
done
compare tac lines.txt words.txt
compare tac nosuch.txt lines.txt
compare_stdin words.txt tac
compare_stdin nonl.txt tac -s a
fi

if want nl; then
section nl
for f in $TEXTS sections.txt; do
    for o in "" -ba -bn -ba\ -l2 -ba\ -l3 -n\ ln -n\ rz -n\ rn -w3 -w10 -s: -s\ -- "-v 10 -i 5" "-v -3" -i-2 -p "-ha -fa" "-ba -ha -fa" "-ba -p" "-d xy -ba" "-d x" "-nrz -w 3 -v 98 -ba"; do
        compare_words nl "$o $f"
    done
done
compare nl words.txt lines.txt
compare nl -ba nonl.txt words.txt
compare nl nosuch.txt lines.txt
compare nl -w0 lines.txt
compare nl -l0 lines.txt
compare nl -n xx lines.txt
compare nl -b x lines.txt
compare_stdin words.txt nl -ba
fi

if want paste; then
section paste
for f in $TEXTS; do
    for o in "" -s "-d :" "-d ',;'" "-s -d ',;'" "-d '\n'" "-d '\0'" "-d '\t\\'" "-s -d '\0'" -z; do
        compare_words paste "$o $f"
    done
done
for o in "" -s "-d ," "-s -d ,:" "-d ''" "-s -d ''" "-d '\'" "-d ab"; do
    for files in "words.txt lines.txt" "lines.txt words.txt dup.txt" "lines.txt empty.txt words.txt" "empty.txt" "nonl.txt lines.txt" "lines.txt lines.txt" "blank.txt dup.txt"; do
        compare_words paste "$o $files"
    done
done
compare paste nosuch.txt lines.txt
# the same standard input twice takes its lines in turn
for o in "" "-s" "-d ,"; do
    for f in lines.txt words.txt nonl.txt; do
        compare_words paste "$o - -" $f
    done
done
compare_stdin lines.txt paste - words.txt -
fi

if want tee; then
section tee
# tee writes files, so these are run one after the other and at once
for f in words.txt nonl.txt empty.txt big.txt; do
    for o in "" -a; do
        total=$((total + 1))
        rm -f exp.f1 exp.f2 got.f1 got.f2
        printf 'old\n' > exp.f1; cp exp.f1 got.f1
        gnu tee $o exp.f1 exp.f2 < $f > exp.out 2> exp.err; erc=$?
        ours tee $o got.f1 got.f2 < $f > got.out 2> got.err; grc=$?
        if ! cmp -s exp.out got.out || ! cmp -s exp.f1 got.f1 || ! cmp -s exp.f2 got.f2 || [ $erc != $grc ]; then
            bad=$((bad + 1))
            echo "DIFFERS: tee $o < $f (status $erc, ours $grc)"
        fi
    done
done
total=$((total + 1))
gnu tee < words.txt > exp.out 2> exp.err; erc=$?
ours tee < words.txt > got.out 2> got.err; grc=$?
if ! cmp -s exp.out got.out || [ $erc != $grc ]; then
    bad=$((bad + 1))
    echo "DIFFERS: tee (no file)"
fi
fi

if want grep; then
section grep
printf 'abc\nabcabc\nxyz\nfoo.bar\nfoo+bar\n(paren)\n[brk]\nA1b2\n a b \n\nfoo  bar\nBAR baz\n' > re.txt
printf '가나다\nabc 한글 def\nÉcole\nécole\nñandú\n한\nx\n' > utf.txt
mkdir -p tree/sub tree/.hid
printf 'needle here\nplain\n' > tree/a.txt
printf 'another needle\n' > tree/sub/b.c
printf 'hidden needle\n' > tree/.hid/c.txt
printf 'bin\0needle\n' > tree/bin.dat

# The whole matrix for two files with varied lines; the files with few lines (a single line without a
# line feed, only empty lines, repeated lines) get a few patterns that behave differently there.
GREP_FILES="words.txt re.txt"
GREP_SMALL_FILES="dup.txt nonl.txt blank.txt"
BRE_PATTERNS=("o" "foo" "^f" "r$" "." "a.c" "x*" "[a-c]" "[^a-c]" "[[:digit:]]\+" "\(ab\)*c" "a\{2\}" "ab\|ba" \
              "\bfoo" "foo\>" "\w\+" "\W" "\s" "o\{1,\}" "^$" "^" "$" "*a" "a\{0\}" "[]a]" "[a-]" "\(a\)\1" \
              "\<a" "o\?" "A" "bar" "^.\{3\}$" "[[:upper:]]" "b\+" "^\(.\)\(.\).\2\1" "fo*" "\.")
ERE_PATTERNS=("a|b" "(ab)+" "a{2,3}" "x?y" "^(abc)+$" "o+" "(foo|bar)+" "^$|x" "a|" "(a)(b)\2" "[0-9]+" \
              "\(paren\)" "^[^ ]+ [^ ]+$" "b{0,1}c" "(^| )foo" ".+" "a**" "()" "(a|b)*c" "foo\.bar" "a{,2}b")
GREP_OPTIONS=("" "-i" "-v" "-c" "-n" "-o" "-w" "-x" "-on" "-b" "-nb" "-vc" "-ci" "-ow" "-m1" "-A1" "-B1" "-C1" "-H" "-q")
SMALL_BRE=("o" "^$" "^" "$" "x*" "a" "\(a\)\1")
SMALL_ERE=("a|b" "^$|x" "a*" "(a|b)+")

for f in $GREP_FILES; do
    for pat in "${BRE_PATTERNS[@]}"; do
        for o in "" -i -v -c -n -o -w -x -m1 -C1 -ow; do
            compare_grep $o -e "$pat" $f
        done
    done
    for pat in "${ERE_PATTERNS[@]}"; do
        for o in "" -i -v -c -o -w -x -n; do
            compare_grep -E $o -e "$pat" $f
        done
    done
    for o in "${GREP_OPTIONS[@]}"; do
        compare_grep $o foo $f
        compare_grep -F $o 'o' $f
    done
done

for f in $GREP_SMALL_FILES; do
    for pat in "${SMALL_BRE[@]}"; do
        for o in "" -i -v -c -n -o -w -x -m1 -C1; do
            compare_grep $o -e "$pat" $f
        done
    done
    for pat in "${SMALL_ERE[@]}"; do
        for o in "" -v -c -o -n; do
            compare_grep -E $o -e "$pat" $f
        done
    done
done

# several files, several patterns, and what a name does to the output
compare_grep foo words.txt re.txt dup.txt
compare_grep -c o words.txt re.txt nosuch.txt
compare_grep -l o words.txt re.txt dup.txt empty.txt
compare_grep -L o words.txt re.txt dup.txt empty.txt
compare_grep -n -H foo re.txt
compare_grep -h foo words.txt re.txt
compare_grep -e foo -e abc -e xyz re.txt
compare_grep -E -e 'a|b' -e 'c' re.txt
compare_grep -F -e foo -e 'o.b' re.txt
compare_grep -F -x -e abc -e xyz re.txt
compare_grep -F -w -e foo -e bar re.txt
compare_grep -f re.txt words.txt
compare_grep -f empty.txt words.txt
compare_grep -v -f empty.txt words.txt
compare_grep -A2 -B2 -n foo re.txt words.txt
compare_grep -C1 -n o dup.txt big.txt
compare_grep -m2 -A1 o words.txt
compare_grep -m0 o words.txt
compare_grep -c -m2 o words.txt
compare_grep -on -b o re.txt
compare_grep -ob -i 'AB' re.txt
compare_grep -2 foo re.txt
compare_grep -A 0 foo re.txt
compare_grep -ni -w -e foo -e baz re.txt
compare_grep -x '' blank.txt
compare_grep -c '' empty.txt
compare_grep -c '^' nonl.txt
compare_grep -w '' re.txt
compare_grep -o '' re.txt
compare_grep -E 'a{1,2}{2}' re.txt
compare_grep -E '{1}' re.txt
compare_grep -E -c '{1}x' re.txt
compare_grep -- -x re.txt
compare_grep -e '' re.txt
compare_grep -z foo re.txt
compare_grep -zc '' re.txt
compare_grep -Z -l foo words.txt re.txt
compare_grep -Z -c foo words.txt re.txt
compare_grep -a foo tree/bin.dat
compare_grep foo tree/bin.dat
compare_grep -c needle tree/bin.dat
compare_grep -I needle tree/bin.dat
compare_grep -r needle tree
compare_grep -rn needle tree/sub
compare_grep -rl needle tree
compare_grep -rc needle tree
compare_grep -rh needle tree
compare_grep -r --include='*.c' needle tree
compare_grep -r --exclude='*.txt' needle tree
compare_grep -r --exclude-dir=sub needle tree
compare_grep needle tree/a.txt tree/sub
compare_grep -s needle tree/sub
compare_grep -d skip needle tree/a.txt tree/sub

# UTF-8: a character is one character
for pat in "." "^.$" "^..$" "[가-힣]" "[^a-z]" "é" "É" "^.\{3\}$" "한" "c 한" "\w\+" "[[:alpha:]]\+"; do
    for o in "" -i -o -c -v -n; do
        compare_grep $o -e "$pat" utf.txt
    done
done
compare_grep -E '^.{2}$' utf.txt
compare_grep -io 'école' utf.txt
compare_grep -F -i 'ÉCOLE' utf.txt

# errors: the status is what counts
for bad_pattern in '\(' '\)' 'a\{' 'a\{1' 'a\{2,1\}' '[' '[a' '[[:foo:]]' '[:alpha:]' '[b-a]' '\1' 'a\' '\(a\)\2'; do
    compare_grep -e "$bad_pattern" re.txt
done
for bad_pattern in '(' ')' 'a{1' 'a{2,1}' '[' '(a' 'a)' '\1' 'a\' '+' '*a' '?' '(*a)' '(+)'; do
    compare_grep -E -e "$bad_pattern" re.txt
done
fi

# ---- run what was collected ---------------------------------------------------------------
if [ -n "$BATCH" ] && [ -f "$CASES" ]; then
    # native paths, as the program is not an MSYS program
    "$BATCH_EXE" "$CASES" "$(cygpath -w "$GNU_DIR")" "$(cygpath -w "$BIN_DIR")"
    bad=$((bad + $?))
fi

echo
echo "$total combinations compared, $bad differ"
exit $bad
