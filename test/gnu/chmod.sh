# GNU comparison of chmod: each case makes a file with a first mode (chmod OLD), runs chmod -v MODE and compares the
# message, the mode stat shows afterwards and the exit status. Windows keeps only the write permission of the owner, so
# the starting modes are chosen with and without it and what stat shows is the same on both sides.
chm_run() {
    local who=$1 old=$2 rc stater
    shift 2
    rm -rf chm_dir
    mkdir chm_dir
    : > chm_dir/f
    mkdir chm_dir/d
    : > chm_dir/p.exe
    ( cd chm_dir && "$who" chmod "$old" f d p.exe )
    ( cd chm_dir && MSYS2_ARG_CONV_EXCL="*" "$who" chmod -v "$@" f d p.exe > ../chm_out.txt 2> ../chm_err.txt ); rc=$?
    cat chm_out.txt
    echo '-- err'; tr -d '\r' < chm_err.txt | sed -e 's#^[^ :]*chmod:#chmod:#' -e "s#^Try '[^ ]*chmod #Try 'chmod #"
    echo "-- rc $rc"
    ( cd chm_dir && $who stat -c '%n %a' f d p.exe 2>&1 )
}
chm_case() {
    local old=$1 g o
    shift
    total=$((total + 1))
    g=$(chm_run gnu "$old" "$@")
    o=$(chm_run ours "$old" "$@")
    if [ "$g" != "$o" ]; then
        bad=$((bad + 1))
        echo "DIFFERS: chmod [$old] $*"
    fi
}
for old in 644 444 755 000 666; do
    for m in 644 755 0 000 7 12 777 600 400 200 u+x u-x u=r u=rw u=rwx g+w g-r g=x o+w o-r o=rx a+x a-x a=r a=rwx a= +x -x +w -w =r =rw =rwx =x +r -r \
             u+x,g+x ugo+x u=g g=u o=u u=o a=u g+u u+rwx,g-w,o= go-rwx u=rwX a+X o-X +rw-x =u -u +u a=g go=u u+r+w u-w+x,g=u u=,g=,o= \
             a+r,a-r u=rw,g=r,o= g+rwx-w a=,; do
        chm_case $old "$m"
    done
done
chm_case 644 -- -w
chm_case 644 -c u-w
chm_case 644 -c u+w
chm_case 444 -c u+w
chm_case 444 -c u-w
chm_case 644 -f u-w
chm_case 644 --changes g+w
chm_case 644 --verbose g+w
chm_case 644 --silent g+w
chm_case 644 --quiet g+w
chm_case 644 xyz
chm_case 644 999
chm_case 644 u+q
chm_case 644 ''
chm_case 644 u+x,
chm_case 644 ,
chm_case 644 8
chm_case 644 10000
chm_case 644 00000
add_case status C - chmod
add_case status C - chmod 644
add_case status C - chmod -v 644 chmod_nosuchfile
add_case out C - chmod -f 644 chmod_nosuchfile
add_case out C - chmod -c 644 chmod_nosuchfile
add_case status C - chmod --bogus 644 f
add_case status C - chmod --reference=chmod_nosuchfile 644
add_case status C - chmod --help
add_case status C - chmod --version
