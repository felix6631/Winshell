#!/bin/bash
#
# Compares the shell of this project with bash: every case of shell/cases.txt is run as a script by both, in an empty
# directory of its own, and the output and the exit status have to be the same. The cases are the patterns an agent
# writes (pipes, lists, substitutions, here documents, loops, functions, traps, expansions, tests, redirections).
# What is meant to be different is not in the cases: the commands are the ones of the project (bin) for the shell and
# those of MSYS for bash, so a case only uses what both do alike.
#
#   bash test/shell_compare.sh                   all the cases
#   SHELL_ONLY=for bash test/shell_compare.sh    the cases whose name contains "for"
#   SHELL_VERBOSE=1 ...                          show the difference of each failing case
#
# A case starts with a line "=== name" and goes up to the next one. The exit code is the number of cases that differ.

HERE=$(cd "$(dirname "$0")" && pwd)
BIN_DIR=${BIN_DIR:-$HERE/../bin}
CASES=${CASES:-$HERE/shell/cases.txt}
export LC_ALL=C

SHELL_EXE="$BIN_DIR/shell.exe"
if [ ! -x "$SHELL_EXE" ]; then
    echo "no $SHELL_EXE (build with nmake first)"
    exit 127
fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# split the cases into files
mkdir "$WORK/cases"
awk -v dir="$WORK/cases" '
    /^=== / { name = substr($0, 5); gsub(/[^A-Za-z0-9_.-]/, "_", name); file = dir "/" name ".sh"; printf "" > file; next }
    file != "" { print >> file }
' "$CASES"

total=0
bad=0
for script in "$WORK"/cases/*.sh; do
    name=$(basename "$script" .sh)
    if [ -n "$SHELL_ONLY" ] && [[ "$name" != *"$SHELL_ONLY"* ]]; then
        continue
    fi
    total=$((total + 1))
    rm -rf "$WORK/exp" "$WORK/got"
    mkdir "$WORK/exp" "$WORK/got"
    (cd "$WORK/exp" && timeout 20 bash "$script" > ../exp.out 2> ../exp.err < /dev/null)
    erc=$?
    (cd "$WORK/got" && timeout 20 "$SHELL_EXE" "$(cygpath -w "$script")" > ../got.out 2> ../got.err < /dev/null)
    grc=$?
    tr -d '\r' < "$WORK/got.out" > "$WORK/got.out.lf"
    if [ $erc != $grc ] || ! cmp -s "$WORK/exp.out" "$WORK/got.out.lf"; then
        bad=$((bad + 1))
        echo "DIFFERS: $name (status $erc, ours $grc)"
        if [ -n "$SHELL_VERBOSE" ]; then
            diff "$WORK/exp.out" "$WORK/got.out.lf" | head -${SHELL_LINES:-12} | sed 's/^/    /'
            head -3 "$WORK/got.err" | sed 's/^/    err: /'
        fi
    fi
done
echo
echo "$total cases compared, $bad differ"
exit $bad
