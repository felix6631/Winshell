# GNU comparison of link and unlink: each case runs in a fresh directory; the files that are left and their link counts
# are compared with the exit statuses.
fs_case() {
    local tool=$1 setup=$2 g o grc orc
    shift 2
    total=$((total + 1))
    for who in gnu ours; do
        rm -rf fs_dir
        mkdir fs_dir
        ( cd fs_dir && eval "$setup" )
        if [ $who = gnu ]; then
            ( cd fs_dir && gnu "$tool" "$@" > ../fs_out.txt 2> /dev/null ); grc=$?
            g=$( cd fs_dir && find . -mindepth 1 | sort | while read -r f; do stat -c '%n %s %h' "$f"; done; cat ../fs_out.txt )
        else
            ( cd fs_dir && ours "$tool" "$@" > ../fs_out.txt 2> /dev/null ); orc=$?
            o=$( cd fs_dir && find . -mindepth 1 | sort | while read -r f; do stat -c '%n %s %h' "$f"; done; cat ../fs_out.txt )
        fi
    done
    if [ "$g" != "$o" ] || [ $grc != $orc ]; then
        bad=$((bad + 1))
        echo "DIFFERS: $tool $* [$setup] (status $grc, ours $orc)"
        echo "--- gnu"; echo "$g"; echo "--- ours"; echo "$o"
    fi
}
export MSYS2_ARG_CONV_EXCL='*'
S='printf hello > a; printf x > b; mkdir d; printf y > d/c'
fs_case link "$S" a z
fs_case link "$S" a b
fs_case link "$S" nosuch z
fs_case link "$S" a d/z
fs_case link "$S" a nodir/z
fs_case link "$S" d z
fs_case link "$S" a
fs_case link "$S"
fs_case link "$S" a b c
fs_case link "$S" a a
fs_case link "$S" d/c d/e
fs_case unlink "$S" a
fs_case unlink "$S" nosuch
fs_case unlink "$S" d
fs_case unlink "$S" d/c
fs_case unlink "$S" a b
fs_case unlink "$S"
fs_case unlink "$S" --bogus
fs_case link "$S" --bogus
add_case status C - link --help
add_case status C - unlink --help
add_case status C - link --version
add_case status C - unlink --version
