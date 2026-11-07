# GNU comparison of mktemp: the names are random, so the exit status and the shape of the name are compared.
shape_case() {
    local pattern=$1 g o
    shift
    total=$((total + 1))
    g=$(gnu mktemp "$@" 2>/dev/null); local grc=$?
    o=$(ours mktemp "$@" 2>/dev/null); local orc=$?
    if [ $grc != $orc ] || ! printf '%s\n' "$g" | grep -Eq "$pattern" || ! printf '%s\n' "$o" | grep -Eq "$pattern"; then
        bad=$((bad + 1))
        echo "DIFFERS: mktemp $* (status $grc, ours $orc; '$g' / '$o')"
    fi
}
shape_case '^a[A-Za-z0-9]{3}$' -u aXXX
shape_case '^tmp\.[A-Za-z0-9]{6}$' -u tmp.XXXXXX
shape_case '^[A-Za-z0-9]{10}\.txt$' -u --suffix=.txt XXXXXXXXXX
shape_case '^pre[A-Za-z0-9]{4}post$' -u --suffix=post preXXXX
shape_case '^[A-Za-z0-9]{3}$' -u XXX
shape_case '^\./x[A-Za-z0-9]{5}$' -u -p . xXXXXX
shape_case '^\./x[A-Za-z0-9]{5}$' -u --tmpdir=. xXXXXX
shape_case '^sub/x[A-Za-z0-9]{5}$' -u -p sub xXXXXX
shape_case '^m[A-Za-z0-9]{3}$' mXXX
shape_case '^md[A-Za-z0-9]{3}$' -d mdXXX
shape_case '^md2[A-Za-z0-9]{3}$' --directory md2XXX
shape_case '^q[A-Za-z0-9]{3}$' -q qXXX
# failures
add_case status C - mktemp aXX
add_case status C - mktemp XX
add_case status C - mktemp abc
add_case status C - mktemp ''
add_case status C - mktemp -u aXX
add_case status C - mktemp nosuchdir/aXXX
add_case status C - mktemp -q nosuchdir/aXXX
add_case status C - mktemp --suffix=a/b aXXX
add_case status C - mktemp -t a/bXXX
add_case status C - mktemp -u -p . /abcXXX
add_case status C - mktemp a b
add_case status C - mktemp --bogus
add_case status C - mktemp -x
add_case status C - mktemp --help
add_case status C - mktemp --version
