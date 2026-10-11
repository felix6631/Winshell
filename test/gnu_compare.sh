#!/bin/bash
#
# Compares head, tail, wc, sort, uniq, cut, tr and cat with the GNU tools: the output bytes and the
# exit codes of many combinations of options and inputs have to be the same.
#
# The GNU tools are the ones of MSYS2 or Git for Windows (an environment with bash). They are
# run with LC_ALL=C, and so is the order of sort of this project.
#
#   bash test/gnu_compare.sh                     from the repository, after nmake
#   GNU_DIR=/usr/bin BIN_DIR=bin bash test/gnu_compare.sh
#
# The exit code is the number of combinations that differ, or 0. What is meant to be different is not
# in the comparisons: a line ending of CR LF is not part of a line here (so inputs with CR LF are
# only compared for the tools whose result does not depend on that), sort -z, -h, -M and -V are
# refused, and a character is a character of UTF-8 (the MSYS locale counts bytes).

GNU_DIR=${GNU_DIR:-/usr/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
BIN_DIR=${BIN_DIR:-$HERE/../bin}
export LC_ALL=C

if [ ! -x "$GNU_DIR/sort.exe" ] && [ ! -x "$GNU_DIR/sort" ]; then
    echo "no GNU tools in $GNU_DIR (set GNU_DIR)"
    exit 127
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
head -c 70000 /dev/urandom | od -An -c > noise.txt 2>/dev/null || cp big.txt noise.txt

TEXTS="words.txt nums.txt table.txt tabs.txt dup.txt fields.txt lines.txt empty.txt nonl.txt blank.txt big.txt"

# ---- comparing ----------------------------------------------------------------------------
total=0
bad=0

gnu() { local t=$1; shift; if [ -x "$GNU_DIR/$t.exe" ]; then "$GNU_DIR/$t.exe" "$@"; else "$GNU_DIR/$t" "$@"; fi; }
ours() { local t=$1; shift; "$BIN_DIR/$t.exe" "$@"; }

# compare TOOL ARGS... : the same arguments for both, the output and the status of both
compare() {
    local tool=$1; shift
    total=$((total + 1))
    gnu "$tool" "$@" > exp.out 2> exp.err < /dev/null; local erc=$?
    ours "$tool" "$@" > got.out 2> got.err < /dev/null; local grc=$?
    if ! cmp -s exp.out got.out || [ $erc != $grc ]; then
        bad=$((bad + 1))
        echo "DIFFERS: $tool $* (status $erc, ours $grc)"
    fi
}

# compare_stdin FILE TOOL ARGS... : the same with the file as standard input
compare_stdin() {
    local file=$1 tool=$2; shift 2
    total=$((total + 1))
    gnu "$tool" "$@" < "$file" > exp.out 2> exp.err; local erc=$?
    ours "$tool" "$@" < "$file" > got.out 2> got.err; local grc=$?
    if ! cmp -s exp.out got.out || [ $erc != $grc ]; then
        bad=$((bad + 1))
        echo "DIFFERS: $tool $* < $file (status $erc, ours $grc)"
    fi
}

echo "== head"
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

echo "== tail"
for f in $TEXTS; do
    for o in "" "-n 3" "-n 0" "-n 100" "-n +3" "-n +1" "-n +100" "-c 7" "-c -5" "-c +4" "-c +1000" "-c 3b" "-3"; do
        compare tail $o $f
    done
done
compare tail -n 2 lines.txt words.txt
compare tail -q -n 2 lines.txt words.txt
compare tail -v -n 2 lines.txt
compare tail nosuch.txt

echo "== wc"
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

echo "== sort"
for f in $TEXTS; do
    for o in "" -r -u -f -b -d -n -rn -nu -s -fu "-k2" "-k1,1" "-k2,2n" "-k2,2nr" "-k1.2" "-k1.2,1.3" \
             "-t: -k2n" "-t: -k2,2n -k1,1r" "-t: -k3 -k1" "-ru" "-c" "-C" "-g" "-i" "-t: -u -k3,3" "-k2b" "-k 2,2 -s"; do
        compare sort $o $f
    done
done
compare sort words.txt nums.txt
compare sort -o out_file.txt words.txt
compare sort nosuch.txt
compare sort -k0 words.txt

echo "== uniq"
for f in dup.txt words.txt fields.txt empty.txt nonl.txt blank.txt; do
    for o in "" -c -d -D -u -i -ci "-f1" "-f 1 -c" "-s 2" "-w 1" "-w 2 -c" "-d -c" "-u -c" "-i -d"; do
        compare uniq $o $f
    done
done
compare_stdin dup.txt uniq -c

echo "== cut"
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

echo "== tr"
for args in "a-z A-Z" "[:lower:] [:upper:]" "[:upper:] [:lower:]" "abc xyz" "abc x" "a-c 12" "-d a-c" "-d [:digit:]" \
            "-d [:space:]" "-s a-z" "-s ' '" "-c a-z _" "-cd a-z" "-cs a-z \\n" "-ds a-c d" "-s a-b A-B" \
            "-t abcd xy" "a-c [x*]" "abc [x*2]" "\\n \\t" "[=a=] b" "a-b -" "-d \\n"; do
    for f in words.txt table.txt; do
        total=$((total + 1))
        eval "gnu tr $args" < $f > exp.out 2> exp.err; erc=$?
        eval "ours tr $args" < $f > got.out 2> got.err; grc=$?
        if ! cmp -s exp.out got.out || [ $erc != $grc ]; then
            bad=$((bad + 1))
            echo "DIFFERS: tr $args < $f (status $erc, ours $grc)"
        fi
    done
done
for args in "" "a" "-d a b" "z-a x" "[:foo:] x" "a-c [x*] y" "[a*] b"; do
    total=$((total + 1))
    eval "gnu tr $args" < words.txt > exp.out 2> exp.err; erc=$?
    eval "ours tr $args" < words.txt > got.out 2> got.err; grc=$?
    if [ $erc != $grc ]; then
        bad=$((bad + 1))
        echo "DIFFERS (status): tr $args (status $erc, ours $grc)"
    fi
done

echo "== cat"
for f in $TEXTS; do
    for o in "" -n -b -s -E -T -v -A -e -t -ns -bs -nE; do
        compare cat $o $f
    done
done
compare cat nosuch.txt lines.txt
compare cat -n lines.txt words.txt

echo
echo "$total combinations compared, $bad differ"
exit $bad
