# GNU comparison of vdir: ls -l -b, so the long format with the owner, the group and the link count left out (those of MSYS
# are numbers it made up) and the blocks; the order, the sizes, the times and the escapes of the names are compared.
mkdir -p vd_dir/sub
printf 'abc' > vd_dir/a.txt
printf 'x' > "vd_dir/sp ace"
printf 'x' > vd_dir/b.c
head -c 2000 /dev/zero > vd_dir/big.dat
touch -d '2020-01-02 03:04:05' vd_dir/a.txt vd_dir/sub
touch -d '2021-05-06 07:08:09' vd_dir/b.c "vd_dir/sp ace"
touch -d '2019-12-31 23:59:59' vd_dir/big.dat
vdir_long() {
    local erc grc
    total=$((total + 1))
    gnu vdir "$@" > exp.raw 2> exp.err < /dev/null; erc=$?
    ours vdir "$@" > got.raw 2> got.err < /dev/null; grc=$?
    sed -E 's/^total .*/total/; s/^([^ ]{10}) +[0-9]+ +[^ ]+ +[^ ]+ +/\1 /' exp.raw > exp.out
    sed -E 's/^total .*/total/; s/^([^ ]{10}) +[0-9]+ +[^ ]+ +[^ ]+ +/\1 /' got.raw > got.out
    if ! cmp -s exp.out got.out || [ $erc != $grc ]; then
        bad=$((bad + 1))
        echo "DIFFERS: vdir $* (status $erc, ours $grc)"
    fi
}
for o in "" -a -A -r -t -S -h -F -p -d "-a -r" "-t -r" "-h -S" -1 -R; do
    vdir_long $o vd_dir
    vdir_long $o vd_dir/sub
done
vdir_long vd_dir/a.txt vd_dir/sub
vdir_long -d vd_dir vd_dir/sub
vdir_long vd_dir/nosuchfile_zz
vdir_long --bogus
add_case status C - vdir --help
add_case status C - vdir --version
