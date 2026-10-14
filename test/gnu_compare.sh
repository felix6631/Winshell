#!/bin/bash
#
# Compares head, tail, wc, sort, uniq, cut, tr, cat, tac, nl, paste, tee, grep, sed, expr, find and xargs with the GNU tools: the output bytes and the
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

if want sed; then
section sed
printf 'apple pie\nbanana split\ncherry tart\napple crumble\n\nplum cake\naab abb aaa\nlast line\n' > s1.txt
printf '가나다 abc\nÉcole école\nxyz\n한글 text\n' > s2.txt
printf 'one\r\ntwo\r\nthree\r\n' > s3.txt
SED_FILES="s1.txt words.txt lines.txt nonl.txt empty.txt blank.txt dup.txt"

compare_sed() { add_case out U - sed "$@"; }

SED_SCRIPTS=('p' 'd' '2d' '2,4d' '$d' '2!d' '1~2d' '2~3p' '0~2d' '/a/d' '/a/,/e/d' '/^$/d' '2,+1d' '2,~4d'
    '0,/a/d' '1,/a/d' '/b/,+1p' '$!N;P;D' 'N;N;s/\n/+/g' 's/a/X/' 's/a/X/g' 's/a/X/2' 's/a/X/2g'
    's/[aeiou]/<&>/g' 's/\(.\)\(.\)/\2\1/' 's/^/> /' 's/$/ </' 's/x*/-/g' 's/b*/X/g' 's/.*/\U&/'
    's/\w\+/\u&/g' 's/\(a\)\|b/[\1]/g' 's/a/\n/' 's/ /\t/g' 'y/abc/xyz/' 'y/ab/\n\t/' '=' 'l' 'l 5' 'l 0'
    'a foo' 'i bar' 'c X' '2c X' '2,3c X' 'x' 'G' 'H;$!d;x' 'h;G' 'g' '1h;1!G;$!d' '$!{h;d};x;G' 'n;d'
    'q' '2q' 'Q' '3Q' '2q5' 'z' 'w /dev/stdout' 's/a/b/w /dev/stdout' 's/a/b/p' 's/a/b/gp' 'p;p'
    '/e/!s/a/b/' ':a;s/aa/a/;ta' '/a/{s//X/;}' 's/a/b/;T;s/$/!/' 'b end;s/a/X/;:end'
    ':a;N;$!ba;s/\n/,/g' 'N;P;D' '$!N;s/\n/ /' 's/APPLE/x/I' '/APPLE/Id' 's/\bp/P/g' 's/p\b/P/g'
    's/a\+/X/g' 's/a\{2\}/X/g' 's/[[:upper:]]/U/g' 's/./&&/' 's/\./!/g' 's|a|/|g' 's,a,\,,g' 's/a/\&/g'
    '/a/s/p/P/' '2,3s/^/#/' '$s/$/ END/' '1!G;h;$!d' 's/\(^\|[^a-z]\)a/\1A/g' '/^$/,$d' '/a/,3p'
    'N;l' 'x;p;x' '1d;n;d' '$!d' '2{p;p}' '2,3{s/a/b/;p}' '/apple/{n;s/^/>/}' '2,$!d' 's/a/b/3' 's/\n//' 'D'
    '/b/,/c/{=;p}' '1,3!s/^/- /' 's/\(a*\)b/<\1>/' 's/^\(.\{3\}\).*/\1/' 'y/abcdefghij/ABCDEFGHIJ/')

for script in "${SED_SCRIPTS[@]}"; do
    for f in $SED_FILES; do
        compare_sed -e "$script" $f
        compare_sed -n -e "$script" $f
    done
done

ERE_SCRIPTS=('s/(a|b)+/X/g' 's/(.)\1/<\1>/g' 's/a{2,}/X/' 's/a?b/X/g' 's/(^| )a/\1A/g' '/^(apple|plum)/d' 's/([a-z]+) ([a-z]+)/\2 \1/'
    's/(a)(b)?/[\2]/' '/a+b/p' 's/[0-9]+/<&>/g' 's/^.{3}//')
for script in "${ERE_SCRIPTS[@]}"; do
    for f in s1.txt words.txt dup.txt; do
        compare_sed -E -e "$script" $f
        compare_sed -r -n -e "$script" $f
    done
done

# UTF-8: a character is a character
for script in 's/./X/' 's/./X/g' 'y/가나/AB/' 's/[가-힣]/H/g' 's/.*/\U&/' 's/É/e/I' 's/\(.\)\(.\)/\2\1/' 'l' '/한/d' 's/é/E/gI'; do
    compare_sed -e "$script" s2.txt
done

# options and several files
compare_sed -n '$p' s1.txt words.txt
compare_sed -s -n '$p' s1.txt words.txt
compare_sed -s -n '1p' s1.txt words.txt
compare_sed -n '1p;$=' s1.txt words.txt
compare_sed -s '1F' s1.txt words.txt
compare_sed -n -s '$=' s1.txt lines.txt
compare_sed -z 's/\n/,/g' s1.txt
compare_sed -z -n 'p' words.txt
compare_sed -n -l 20 l s1.txt
compare_sed --expression='s/a/b/' --expression='s/b/c/' s1.txt
compare_sed -e 'a\' -e 'text' s1.txt
compare_sed -e '1i\' -e '  indented' s1.txt
compare_sed '2{;p;}' s1.txt
compare_sed -n -e '/apple/{p;q}' s1.txt
compare_sed -f nosuch.sed s1.txt
compare_sed p nosuch.txt
compare_sed p nosuch.txt s1.txt
compare_sed -n '#n
p' s1.txt
compare_sed '#n
p' s1.txt
compare_sed -e 'r lines.txt' -e '2q' s1.txt
compare_sed '2r lines.txt' s1.txt
compare_sed 'R lines.txt' s1.txt
compare_sed -n 'W /dev/stdout' s1.txt
compare_sed 's/a/X/;ta;s/$/!/;:a' s1.txt
compare_sed '/a/{N;N;s/\n/|/g}' s1.txt
compare_sed -s -n '0~2p;$=' s1.txt words.txt

# errors: the status counts
for bad_script in 's/a/b' 'k' '{p' 'p}' 'b nolabel' '0p' 's/\(a/b/' 's/a/\1/' 'y/ab/c/' 'a' 's/a/b/gg' 's/a/b/pp' 's/a/b/0' 'p p' ',p' '1,p' '2,1!' 's/a/b/z' ':' 'r' 'w' '1,2=' '0,5p' '0~3p' 'y/a/' '/a' '\' 's' 'u'; do
    compare_sed "$bad_script" s1.txt
done
fi

if want expr; then
section expr
compare_expr() { add_case out U - expr "$@"; }

for e in "1 + 2" "5 - 8" "6 \* 7" "7 / 2" "-7 / 2" "7 % 3" "-7 % 3" "7 % -3" "0 - 0" "007 + 1" "-0" "- 3 + 1" "1 / 0" "1 % 0" "a + 1" "1 +" \
         "99999999999999999999 + 1" "123456789012345678901234567890 \* 98765432109876543210" "100000000000000000000 / 7" \
         "100000000000000000000 % 7" "-100000000000000000000 / 3" "-100000000000000000000 % 3" "1000000000 \* 1000000000" \
         "999999999 + 1" "1000000000 - 1" "18446744073709551616 / 18446744073709551616" "5 / 100000000000000000000" \
         "1 \< 2" "2 \< 1" "10 \< 9" "a \< b" "10 \< 9a" "abc = abc" "007 = 7" "1 != 2" "1 == 1" "2 \>= 2" "3 \<= 2" "b \> a" \
         "1 \| 2" "0 \| 2" "'' \| 3" "'' \| ''" "0 \| 0" "a \| b" "1 \& 2" "0 \& 2" "1 \& 0" "'' \& 1" "a \& b" "1 \| 1 / 0" "0 \& 1 / 0" \
         "1 + 2 \* 3" "\( 1 + 2 \) \* 3" "2 \* \( 3 + 4 \) - 5" "\( 1" "1 \)" "\( \)" "1 2" "1 + 2 3" \
         "abc : abc" "abc : a" "abc : b" "abc : '.*'" "abc : 'a\(.\)'" "abc : '\(.\)\(.\)'" "abc : 'x\(.\)'" "abc : 'x'" "abc : ''" \
         "abc : 'a\|b'" "aaa : 'a*'" "abc : '[a-c]*'" "abc : '\(a\|x\)\(b\)'" "a.c : 'a\.c'" "abc : '^a'" "abc : 'c$'" "abc : 'abc$'" \
         "abc : '\(a'" "abc : 'a\)'" "abc : '\1'" "abc : 'a\{2\}'" "aab : 'a\{2\}b'" "x : '*'" "a+b : 'a+b'" "ab : 'a\+b'" \
         "match abc 'a.'" "match abc 'b'" "substr hello 2 3" "substr hello 0 3" "substr hello 1 0" "substr hello 5 10" "substr hello 6 1" \
         "substr hello -1 3" "substr hello 2 -1" "substr hello a 1" "index hello l" "index hello xyz" "index hello ol" "length hello" \
         "length ''" "length 'a b'" "+ match" "+ length abc" "+ + 1" "+ 1 + 2" "length + 1" "match" "substr hello 1" "length" \
         "1 = 1 = 1" "3 \> 2 \> 1" "2 + 3 : '.*'" "'a b' : 'a b'" "length 1234567" "- 5" "-5 + 10" "-- 5" "5 -" ; do
    eval "set -- $e"
    compare_expr "$@"
done

# UTF-8: characters
for e in "length 가나다" "substr 가나다 2 1" "index 가나다 다" "가나다 : '.*'" "가나다 : '\(.\)'" "가나다 : '가\(.\)'" "héllo : 'h.l'" ; do
    eval "set -- $e"
    compare_expr "$@"
done

add_case status C - expr
add_case status C - expr --help
fi

if want find; then
section find
mkdir -p ftree/src/lib ftree/docs ftree/empty_dir ftree/.hidden ftree/deep/er/est
printf 'int main(){}\n' > ftree/src/main.c
printf 'void f(){}\n' > ftree/src/lib/util.c
printf '#define X\n' > ftree/src/lib/util.h
printf 'hello\n' > ftree/docs/README.md
printf 'notes notes notes notes notes\n' > ftree/docs/notes.TXT
printf '' > ftree/docs/zero.txt
printf 'secret\n' > ftree/.hidden/key
printf 'x' > ftree/deep/er/est/bottom.txt
head -c 3000 /dev/zero > ftree/big.bin
touch -d '2020-01-01 00:00:00' ftree/old.txt
touch -d '2021-06-15 12:00:00' ftree/docs/notes.TXT
touch ftree/new.txt
touch -d '2 days ago' ftree/two_days.txt
touch -d '3 hours ago' ftree/hours.txt

compare_find() { add_case out U - find "$@"; }

compare_find ftree
compare_find ftree/src ftree/docs
compare_find ftree -name '*.c'
compare_find ftree -name '*.[ch]'
compare_find ftree -iname '*.txt'
compare_find ftree -name 'README*' -o -name 'util*'
compare_find ftree -name '?ain.c'
compare_find ftree -name '.*'
compare_find ftree -path '*/lib/*'
compare_find ftree -ipath '*/DOCS/*'
compare_find ftree -path 'ftree/src*' -name '*.c'
compare_find ftree -type f
compare_find ftree -type d
compare_find ftree -type f -name '*.txt'
compare_find ftree -type d -empty
compare_find ftree -type f -empty
compare_find ftree -empty
compare_find ftree -maxdepth 0
compare_find ftree -maxdepth 1
compare_find ftree -maxdepth 2 -type f
compare_find ftree -mindepth 2
compare_find ftree -mindepth 2 -maxdepth 3
compare_find ftree -depth
compare_find ftree -depth -type d
compare_find ftree -size 0
compare_find ftree -size -1
compare_find ftree -size +1k
compare_find ftree -size 3000c
compare_find ftree -size +2000c -size -4000c
compare_find ftree -size 1
compare_find ftree -size 6
compare_find ftree -newer ftree/old.txt -type f
compare_find ftree -newer ftree/new.txt
compare_find ftree -mtime +1000 -type f
compare_find ftree -mtime 0 -type f
compare_find ftree -mtime -1 -type f
compare_find ftree -mtime 2 -type f
compare_find ftree -mtime +1 -type f
compare_find ftree -mmin -1 -type f
compare_find ftree -mmin +60 -mmin -300 -type f
compare_find ftree -regex '.*\.c'
compare_find ftree -regex 'ftree/src/.*'
compare_find ftree -regextype posix-extended -regex '.*/(main|util)\.[ch]'
compare_find ftree -regex '.*\(main\|util\)\.c'
compare_find ftree -iregex '.*readme.*'
compare_find ftree -regex '.*lib'
compare_find ftree -prune
compare_find ftree -name lib -prune -o -type f -print
compare_find ftree -name docs -prune -o -name '*.txt' -print
compare_find ftree -type f -name '*.c' -print
compare_find ftree -type f -print0
compare_find ftree -type f -name '*.c' -o -name '*.h'
compare_find ftree '(' -name '*.c' -o -name '*.h' ')' -type f
compare_find ftree ! -name '*.txt' -type f
compare_find ftree -not -type d
compare_find ftree -type f -a -name '*.md'
compare_find ftree -name main.c , -name README.md
compare_find ftree -true
compare_find ftree -false
compare_find ftree -name '*.c' -quit
compare_find ftree -type f -printf '%p|%f|%h|%s|%d|%y\n'
compare_find ftree -maxdepth 1 -printf '%P\n'
compare_find ftree/src -printf '%P\n'
compare_find ftree/src/ -printf '%p\n'
compare_find ftree -type f -name '*.c' -exec echo {} ';'
compare_find ftree -type f -name '*.c' -exec echo found {} +
compare_find ftree -type f -name '*.c' -exec echo a{}b {} ';'
compare_find ftree -type f -name '*.c' -exec false ';' -print
compare_find ftree -type f -name '*.c' -exec true ';' -print
compare_find ftree nosuch
compare_find nosuch ftree/src
compare_find ftree -bogus
compare_find ftree -name
compare_find ftree -size abc
compare_find ftree -maxdepth x
compare_find ftree -type x
compare_find ftree '(' -name x
compare_find ftree -name x ')'
compare_find ftree -exec echo
compare_find ftree -regex '\('
compare_find ftree/src/main.c ftree/docs/README.md
compare_find ftree/src/main.c -name main.c
compare_find ftree/.hidden
compare_find ftree -name '*가*'

fi

if want xargs; then
section xargs
printf 'a b\nc d e\n\nf\n' > x1.txt
printf "a 'b c' \"d e\" f\\\\ g\n" > xquote.txt
printf 'one\0two words\0\0three\0' > x0.bin
printf 'a,b,c\nd,e\n' > xcomma.txt
printf 'a b \nc\nd e\n  f  \n' > xcont.txt
printf '' > xempty.txt
printf 'one two three four five six seven\n' > xlong.txt
printf "unmatched 'quote\n" > xbad.txt
printf 'x\ny\nSTOP\nz\n' > xeof.txt

for f in x1.txt words.txt xquote.txt xcont.txt xlong.txt xeof.txt; do
    for o in "" "-n 1" "-n 2" "-n 3" "-L 1" "-L 2" "-l" "-l2" "-I {}" "-I @ -n 5" "-s 30" "-s 12" "-t" "-r" "-E STOP" "-eSTOP" "-x -n 2" "-P 2" "-P 1 -n 1"; do
        set -- $o
        add_case out C $f xargs "$@" echo X
    done
done
add_case out C x1.txt xargs
add_case out C x1.txt xargs -n 2
add_case out C x1.txt xargs -I {} echo "[{}]" end
add_case out C x1.txt xargs -I[] echo "[]-[]"
add_case out C x1.txt xargs -i echo "<{}>"
add_case out C xempty.txt xargs echo none
add_case out C xempty.txt xargs -r echo none
add_case out C xempty.txt xargs -I {} echo none
add_case out C x0.bin xargs -0 -n 1 echo
add_case out C x0.bin xargs -0 echo
add_case out C x0.bin xargs -0 -I {} echo "<{}>"
add_case out C xcomma.txt xargs -d , echo
add_case out C xcomma.txt xargs -d , -n 1 echo
add_case out C xcomma.txt xargs -d '\n' echo
add_case out C xcomma.txt xargs -d '\n' -n 1 echo
add_case out C xcont.txt xargs -L 1 echo
add_case out C xcont.txt xargs -L 2 echo
add_case out C xcont.txt xargs -I {} echo "<{}>"
add_case out C xbad.txt xargs echo
add_case out C xquote.txt xargs -0 echo
add_case out C x1.txt xargs -a x1.txt echo
add_case out C x1.txt xargs --arg-file=xcont.txt -n 1 echo
add_case out C x1.txt xargs -a nosuch.txt echo
add_case out C x1.txt xargs nosuchprogram_xyz
add_case out C x1.txt xargs -n 0 echo
add_case out C x1.txt xargs -L 0 echo
add_case out C x1.txt xargs -s 1 echo
add_case out C x1.txt xargs -x -s 3 echo
add_case out C x1.txt xargs -n 1 false
add_case out C x1.txt xargs true
add_case out C x1.txt xargs -n 1 sh -c 'exit 255' x
add_case out C x1.txt xargs -n 1 sh -c 'test "$1" != c' x
add_case out C x1.txt xargs -n 2 printf '%s|'
add_case out C xlong.txt xargs --max-args=3 --max-chars=100 echo
add_case out C xlong.txt xargs --no-run-if-empty --verbose echo
fi

if want basename; then
section basename
# a name of two slashes is a root of its own for Cygwin (//server); here it is "/", as on Linux
for n in "/usr/bin/sort" "sort" "/" "///" "a/" "a//" "/a" "//a//" "a/b/c" "a/b/c/" "" "." ".." "./a" "a." ".hidden" "a b/c d" "/a/b.txt" "dir/.txt" "x.h.h" "가/나다.txt"; do
    compare basename -- "$n"
    compare basename -a -- "$n"
    compare basename -z -- "$n"
    for suffix in ".txt" ".h" "txt" "a" "sort" "" "/" ".h.h" "나다.txt"; do
        compare basename -- "$n" "$suffix"
        compare basename -s "$suffix" -- "$n"
    done
done
compare basename -a /a/b.c /d/e.c x
compare basename -s .c /a/b.c /d/e.c x.c
compare basename -z -a a/b c/d
compare basename -as .c a/b.c
compare basename --suffix=.c --multiple a/b.c
compare basename
compare basename a b c
compare basename -a
compare basename -s
compare basename --bogus a
fi

if want dirname; then
section dirname
for n in "/usr/bin/sort" "sort" "/" "///" "a/" "a//" "/a" "///a//" "a/b/c" "a/b/c/" "a//b" "a///b//" "" "." ".." "./a" "a/." "a/.." "/a/b" "a b/c d" "가/나다.txt" "/가"; do
    compare dirname -- "$n"
    compare dirname -z -- "$n"
done
compare dirname /a/b c/d e
compare dirname -z /a/b c/d
compare dirname
compare dirname --bogus a
fi

if want date; then
section date
# The zone name is left out (%Z, and the default format): MSYS has none without TZ, Windows names it.
DATE_FORMATS=('+%a|%A|%b|%B|%h|%d|%e|%j|%m|%y|%Y|%C' '+%H|%I|%k|%l|%M|%S|%p|%P|%s|%N' '+%u|%w|%U|%W|%V|%g|%G'
    '+%D|%F|%T|%R|%r|%x|%X|%c' '+%z|%:z|%::z|%:::z' '+%-d|%-m|%_d|%_m|%0e|%^a|%^B|%#b|%#p|%10Y|%5d|%-5d|%_5d|%05e'
    '+%3N|%6N|%1N|%%|%t|%n|%q|%' '+%5a|%-5a|%010A|%^10b|%e%e|%-H|%-I|%-j|%_j|%03u' '+%5s|%-s')
DATE_INSTANTS=(0 1 59 86399 86400 951782400 1000000000 1234567890 1609459199 1609459200 1609545600 1704067200 \
               1735603200 1735689599 1782864000 1893456000 4102444799 -1 -86400)
for t in "${DATE_INSTANTS[@]}"; do
    for fmt in "${DATE_FORMATS[@]}"; do
        compare date -d "@$t" "$fmt"
        compare date -u -d "@$t" "$fmt"
    done
done
# far from the years of the C library: UTC only (the rules of summer time are not known there)
for t in -2208988800 -62135596800 253402300799 32535215999 32535216000; do
    for fmt in "${DATE_FORMATS[@]}"; do
        compare date -u -d "@$t" "$fmt"
    done
done
for d in "2026-10-14" "2026-10-14 13:45:10" "2026-10-14 13:45" "2026-10-14T13:45:10" "2026-10-14T13:45:10Z" "2026-10-14T13:45:10+02:00" \
         "2026-10-14T13:45:10-0530" "2020-02-29" "2021-01-03" "2020-12-31 23:59:59" "1999-12-31" "2000-01-01 00:00:00"; do
    compare date -d "$d" '+%F %T %s %z'
    compare date -u -d "$d" '+%F %T %s %z'
done
for d in "Oct 14 2026" "14 Oct 2026" "Oct 14, 2026" "October 14 2026 10:00" "1 Jan 2020" "2026-10-14 +2 days" "2026-10-14 3 weeks ago" "2026-10-31 +1 month" "2026-01-31 1 month" "2024-02-29 next year" \
         "2026-10-14 12:00 2 hours ago" "2026-10-14 next day" "2026-10-14 last day" \
         "2026-10-14 yesterday" "2026-10-14 tomorrow" "2026-10-14 monday" "2026-10-14 next monday" "2026-10-14 last friday" \
         "2026-10-14 wed" "2026-10-14 10:30 pm" "2026-10-14 12:30 am" "@1700000000" "@0" "@-5" "@1.5"; do
    compare date -u -d "$d" '+%F %T'
done
for bad in "2026-13-01" "10:99" "@" "@x" "yesterdayx"; do
    compare date -u -d "$bad" '+%F %T'
done
compare date -u -d @1700000000 -I
compare date -u -d @1700000000 --iso-8601=hours
compare date -u -d @1700000000 --iso-8601=minutes
compare date -u -d @1700000000 --iso-8601=seconds
compare date -u -d @1700000000 --iso-8601=ns
compare date -u -d @1700000000 -Idate
compare date -u -d @1700000000 --iso-8601=bogus
compare date -u -d @1700000000 -R
compare date -u -d @1700000000 --rfc-email
compare date -u -d @1700000000 --rfc-3339=date
compare date -u -d @1700000000 --rfc-3339=seconds
compare date -u -d @1700000000 --rfc-3339=ns
compare date -u -d @1700000000 --rfc-3339=bogus
compare date -d @1700000000 -I
compare date -d @1700000000 -R
compare date -d @1700000000 --rfc-3339=seconds
compare date -d @1700000000 --iso-8601=seconds
compare date -u -d @1 -d @2 '+%s'
compare date -u -d @1 -r words.txt '+%s'
compare date -r nosuch.txt '+%s'
compare date -u '+%s' extra
compare date extra
compare date -u -d @1 '+%s' '+%F'
compare date -u -d @1 -I '+%s'
compare date --bogus
fi

if want which; then
section which
# The paths differ (/usr/bin/sort and /c/Users/.../sort.exe): the exit status is what is compared.
for n in sort ls cat cmd notepad sh bash grep sed find nosuchprogram_xyz x.y ./nosuch "" ; do
    add_case status C - which "$n"
done
add_case status C - which sort ls nosuch_xyz
add_case status C - which nosuch_xyz sort
add_case status C - which -a sort
add_case status C - which -a nosuch_xyz
add_case status C - which
fi

if want sleep; then
section sleep
for n in 0 0.001 0s 0m 0.0001h 0.00001d 1e-3 .001 0.001s0 "0 0" "0.001 0.001s" abc 1x -1 "" 1.2.3 0x1 inf2 "0 abc" 1ss; do
    compare sleep "$n"
done
compare sleep
compare sleep --bogus
fi

if want realpath; then
section realpath
# The absolute paths of MSYS and of this project differ (/tmp/... and /c/Users/...): the results are compared as
# paths relative to the current directory.
mkdir -p rp/dir/sub rp/other
printf 'x\n' > rp/file.txt
printf 'y\n' > rp/dir/inner.txt

for n in "." ".." "rp" "rp/" "rp//dir" "rp/./dir/../file.txt" "rp/dir/sub/../.." "rp/dir/sub/../../other" "rp/file.txt" "rp/nosuch" \
         "rp/nosuch/deeper" "rp/nosuch/.." "rp/file.txt/x" "rp/file.txt/.." "rp/dir/inner.txt/" "./rp" "rp/dir/../dir/sub" \
         "rp/DIR" "rp/Dir/Sub" "rp/a b" "rp/한글" "" "rp/./././dir" "rp/dir/." "rp/dir/.."; do
    compare realpath --relative-to=. -- "$n"
    compare realpath -e --relative-to=. -- "$n"
    compare realpath -m --relative-to=. -- "$n"
    compare realpath -s --relative-to=. -- "$n"
    compare realpath -q --relative-to=. -- "$n"
    compare realpath -z --relative-to=. -- "$n"
done
compare realpath --relative-to=. rp rp/dir rp/dir/sub rp/nosuch
compare realpath --relative-to=rp/dir rp/file.txt rp/dir/sub rp/other rp
compare realpath --relative-to=rp/dir rp/dir
compare realpath --relative-to=rp/dir/sub rp/other/x
compare realpath -m --relative-to=rp/nosuch rp/nosuch2/a
compare realpath --relative-to=rp/nosuch rp
compare realpath --relative-to=rp/dir --relative-base=rp rp/file.txt rp/dir/sub
compare realpath --relative-to=rp/dir --relative-base=rp rp/dir/sub rp/dir/sub/../../dir
compare realpath --relative-base=rp rp/dir/sub rp/dir
compare realpath --relative-base=. rp/dir
compare realpath
compare realpath --bogus rp
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
