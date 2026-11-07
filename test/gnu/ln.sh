# GNU comparison of ln, for hard links (a symbolic link of MSYS is not one of Windows): each case runs in a fresh
# directory and the files that are left, their link counts and the output are compared with the exit statuses.
ln_case() {
    local setup=$1 g o grc orc who
    shift
    total=$((total + 1))
    for who in gnu ours; do
        rm -rf ln_dir
        mkdir ln_dir
        ( cd ln_dir && eval "$setup" )
        if [ $who = gnu ]; then
            ( cd ln_dir && gnu ln "$@" > ../ln_out.txt 2> /dev/null ); grc=$?
            g=$( cd ln_dir && find . -mindepth 1 | sort | while read -r f; do stat -c '%n %s %h' "$f"; done; cat ../ln_out.txt )
        else
            ( cd ln_dir && ours ln "$@" > ../ln_out.txt 2> /dev/null ); orc=$?
            o=$( cd ln_dir && find . -mindepth 1 | sort | while read -r f; do stat -c '%n %s %h' "$f"; done; cat ../ln_out.txt )
        fi
    done
    if [ "$g" != "$o" ] || [ $grc != $orc ]; then
        bad=$((bad + 1))
        echo "DIFFERS: ln $* [$setup] (status $grc, ours $orc)"
        echo "--- gnu"; echo "$g"; echo "--- ours"; echo "$o"
    fi
}
export MSYS2_ARG_CONV_EXCL='*'
S='printf hello > a; printf x > b; mkdir d; printf y > d/c; printf z > d/e'
ln_case "$S" a z
ln_case "$S" a b
ln_case "$S" -f a b
ln_case "$S" -fv a b
ln_case "$S" -v a z
ln_case "$S" -i a b < /dev/null
ln_case "$S" a d
ln_case "$S" a d/
ln_case "$S" a b d
ln_case "$S" a b z
ln_case "$S" -t d a b
ln_case "$S" -T a z
ln_case "$S" -T a d
ln_case "$S" -t nodir a
ln_case "$S" a
ln_case "$S" d/c
ln_case "$S" d/c d/e
ln_case "$S" -f d/c d/e
ln_case "$S" -b -f a b
ln_case "$S" -bS .orig -f a b
ln_case "$S" --backup=none -f a b
ln_case "$S" nosuch z
ln_case "$S" d z
ln_case "$S" a a
ln_case "$S" -f a a
ln_case "$S" a d/c
ln_case "$S" -fT a d
ln_case "$S" -vt d a b
ln_case "$S"
ln_case "$S" --bogus
ln_case "$S" -t d
ln_case "$S" -T a b c
ln_case "$S" -Tt d a b
add_case status C - ln --help
add_case status C - ln --version
