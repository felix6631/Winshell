#!/bin/bash
#
# Compares the tools of this project with the GNU tools: the output bytes and the exit codes of many
# combinations of options and inputs have to be the same. Each tool is one file of gnu/ (gnu/sort.sh
# compares sort); a new tool only adds its file there.
#
# The GNU tools are the ones of MSYS2 or Git for Windows (an environment with bash). They are
# run with LC_ALL=C, and so is the order of sort of this project (grep: LC_ALL=C.UTF-8).
#
#   bash test/gnu_compare.sh                     from the repository, after nmake
#   GNU_DIR=/usr/bin BIN_DIR=bin bash test/gnu_compare.sh
#   GNU_ONLY=grep bash test/gnu_compare.sh       one section: the name of a file of gnu/ without .sh
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

# ---- the sections: every file of gnu/ is one tool (a new tool only adds a file) ---------------------
for section_file in "$HERE"/gnu/*.sh; do
    tool=$(basename "$section_file" .sh)
    want "$tool" || continue
    section "$tool"
    . "$section_file"
done

# ---- run what was collected ---------------------------------------------------------------
if [ -n "$BATCH" ] && [ -f "$CASES" ]; then
    # native paths, as the program is not an MSYS program
    "$BATCH_EXE" "$CASES" "$(cygpath -w "$GNU_DIR")" "$(cygpath -w "$BIN_DIR")"
    bad=$((bad + $?))
fi

echo
echo "$total combinations compared, $bad differ"
exit $bad
