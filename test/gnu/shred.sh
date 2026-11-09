# GNU comparison of shred: each case shreds files in a fresh directory and compares the messages (-v), the exit status
# and which files are left. The sizes are compared only when -x, -s or a removal makes them independent of the block
# size (the block size of the file system differs), and the contents only for -z where they are zeros.
shred_run() {
    local who=$1 sizes=$2 rc f
    shift 2
    rm -rf sh_dir
    mkdir sh_dir
    printf 'sixteen bytes!!\n' > sh_dir/f
    printf 'abc' > sh_dir/g
    printf 'a-longer-file-name' > sh_dir/longname.txt
    : > sh_dir/empty
    printf 'ro' > sh_dir/ro
    chmod 444 sh_dir/ro
    mkdir sh_dir/d
    ( cd sh_dir && MSYS2_ARG_CONV_EXCL="*" "$who" shred "$@" > ../sh_out.txt 2> ../sh_err.txt ); rc=$?
    ( cd sh_dir && ls | while IFS= read -r f; do
        if [ "$sizes" = 1 ] && [ -f "$f" ]; then echo "$f $(wc -c < "$f")"; else echo "$f"; fi
    done )
    echo '-- out'; cat sh_out.txt
    echo '-- err'; tr -d '\r' < sh_err.txt | sed -e 's#^[^ :]*shred:#shred:#' -e "s#^Try '[^ ]*shred #Try 'shred #"
    echo "-- rc $rc"
}
shred_case() {
    local sizes=0 a g o
    total=$((total + 1))
    for a in "$@"; do
        case "$a" in -x|-s|-u|--remove*|-*[xu]*|--exact|--size*) sizes=1;; esac
    done
    g=$(shred_run gnu $sizes "$@")
    o=$(shred_run ours $sizes "$@")
    if [ "$g" != "$o" ]; then
        bad=$((bad + 1))
        echo "DIFFERS: shred $*"
    fi
}
shred_case -v f
shred_case -vn1 f
shred_case -vn2 f
shred_case -vn3 f
shred_case -vn0 f
shred_case -n0 f
shred_case -vz f
shred_case -vzn1 f
shred_case -vzn0 f
shred_case -vx f
shred_case -vxn1 f
shred_case -v -x -n 2 -z f
shred_case -v empty
shred_case -vx empty
shred_case -vu f
shred_case -vxu f
shred_case -vu g
shred_case -vu longname.txt
shred_case -vu empty
shred_case -vu f g
shred_case -vxu f g longname.txt empty
shred_case -v --remove=unlink f
shred_case -v --remove=wipe f g
shred_case -v --remove=wipesync g
shred_case -v --remove=wipe longname.txt
shred_case -v --remove f
shred_case -v --remove=u f
shred_case -v --remove=w f
shred_case -v --remove=wipes f
shred_case --remove=bogus f
shred_case --remove= f
shred_case -s 4 -v f
shred_case -s 4 -n 1 f
shred_case -s 0 -n 1 f
shred_case -s 1K -n 1 -x f
shred_case -s 20 -n 1 f
shred_case -s 100 -vn1 g
shred_case -s 2x f
shred_case -s -3 f
shred_case -s x f
shred_case -s '' f
shred_case -s 1b -n 1 g
shred_case -s 1c -n 1 g
shred_case --size=3 -vn1 f
shred_case -n x f
shred_case -n -1 f
shred_case -n 1x f
shred_case -n '' f
shred_case -n 5000000000000000000000 f
shred_case -n 18446744073709551616 f
shred_case --iterations=2 -v f
shred_case -n2 -v f
shred_case nosuchfile
shred_case -v nosuchfile f
shred_case -v f nosuchfile g
shred_case d
shred_case -v d f
shred_case ro
shred_case -f ro
shred_case -fv ro
shred_case -fvu ro
shred_case
shred_case -f
shred_case -- f
shred_case -v -- f g
shred_case -v f f
shred_case -vu f f
shred_case --verbose --force --exact --zero -n1 f
add_case status C - shred --help
add_case status C - shred --version
add_case status C - shred --bogus f
add_case status C - shred -Q f
