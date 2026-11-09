# GNU comparison of dd: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, compare_now, ...).
# The statistics go to standard error and are not compared. Copies to standard output are compared as bytes; copies
# to a file (ddfile) by the content of the file, made from a start file so that seek / notrunc can be seen.
head -c 5000 /dev/urandom > dd_rand.bin
printf 'one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten' > dd_lines.txt
printf 'line one  \nshort\n\nthis line is much longer than twenty bytes\nend\n' > dd_blk.txt
printf 'ab   \ncdef \n    \n' > dd_unblk.txt
for b in $(seq 0 255); do printf "\\$(printf '%03o' $b)"; done > dd_all.bin
printf 'hello world, this is a text to swap in pairs' > dd_swab.txt
printf 'Hello World MiXeD cAsE 123\n' > dd_case.txt
printf 'START-OF-THE-OUTPUT-FILE-WITH-SOME-CONTENT-0123456789' > dd_start.txt

head -c 300 /dev/zero > dd_zero.bin
{ head -c 128 /dev/zero; printf "data"; head -c 200 /dev/zero; } > dd_mixzero.bin
ddfile() {
    local erc grc
    total=$((total + 1))
    cp dd_start.txt dd_exp.out; cp dd_start.txt dd_got.out
    gnu dd "$@" of=dd_exp.out > exp.out 2> exp.err < /dev/null; erc=$?
    ours dd "$@" of=dd_got.out > got.out 2> got.err < /dev/null; grc=$?
    if ! cmp -s dd_exp.out dd_got.out || ! cmp -s exp.out got.out || [ $erc != $grc ]; then
        bad=$((bad + 1))
        echo "DIFFERS: dd $* of=FILE (status $erc, ours $grc)"
    fi
}

for f in dd_rand.bin dd_lines.txt dd_all.bin words.txt empty.txt; do
    compare dd if=$f
    compare dd if=$f status=none
    compare dd if=$f bs=1
    compare dd if=$f bs=7
    compare dd if=$f bs=100 count=3
    compare dd if=$f bs=100 skip=2
    compare dd if=$f bs=100 skip=2 count=5
    compare dd if=$f ibs=64 obs=100
    compare dd if=$f ibs=100 obs=64
    compare dd if=$f ibs=3 obs=5 count=10
    compare dd if=$f bs=1k count=1
    compare dd if=$f bs=1kB count=1
    compare dd if=$f bs=2b
    compare dd if=$f bs=1x7
    compare dd if=$f bs=3x4 count=2
    compare dd if=$f count=0
    compare dd if=$f skip=1000
    compare dd if=$f bs=50 conv=sync
    compare dd if=$f ibs=50 obs=70 conv=sync
    compare dd if=$f bs=64 conv=sync,noerror
    compare dd if=$f conv=ucase
    compare dd if=$f conv=lcase
    compare dd if=$f conv=swab
    compare dd if=$f bs=3 conv=swab
    compare dd if=$f ibs=3 obs=4 conv=swab
    compare dd if=$f conv=ascii
    compare dd if=$f conv=ebcdic
    compare dd if=$f conv=ibm
    compare dd if=$f conv=ebcdic,ucase
    compare dd if=$f conv=ascii,lcase
    compare dd if=$f conv=ibm,lcase
    compare dd if=$f iflag=fullblock bs=10
    compare dd if=$f iflag=skip_bytes skip=7 bs=10
    compare dd if=$f iflag=count_bytes count=33 bs=10
    compare dd if=$f iflag=skip_bytes,count_bytes skip=5 count=20 bs=8
    compare_stdin $f dd
    compare_stdin $f dd bs=100
    compare_stdin $f dd ibs=1 obs=16
    compare_stdin $f dd skip=2 bs=16
    compare_stdin $f dd skip=2 count=3 bs=16
    compare_stdin $f dd iflag=fullblock bs=64
done
for f in dd_blk.txt dd_lines.txt words.txt; do
    for cbs in 1 5 10 20 80; do
        compare dd if=$f conv=block cbs=$cbs
        compare dd if=$f conv=block cbs=$cbs bs=3
        compare dd if=$f conv=block,ucase cbs=$cbs
        compare dd if=$f conv=sync,block cbs=$cbs bs=16
    done
done
for f in dd_unblk.txt dd_blk.txt; do
    for cbs in 1 5 6 20; do
        compare dd if=$f conv=unblock cbs=$cbs
        compare dd if=$f conv=unblock cbs=$cbs bs=4
    done
done
compare dd if=dd_swab.txt conv=swab
compare dd if=dd_swab.txt conv=swab,ucase
compare dd if=dd_case.txt conv=ucase
compare dd if=dd_case.txt conv=lcase
compare dd if=dd_case.txt conv=ucase,lcase
compare dd if=dd_all.bin conv=ascii,ebcdic
compare dd if=dd_all.bin conv=block
compare dd if=dd_all.bin conv=unblock
compare dd if=dd_all.bin conv=block,unblock cbs=4

# into a file
ddfile if=dd_rand.bin bs=100 count=2
ddfile if=dd_lines.txt
ddfile if=dd_lines.txt conv=notrunc
ddfile if=dd_lines.txt seek=2 bs=4
ddfile if=dd_lines.txt seek=2 bs=4 conv=notrunc
ddfile if=dd_lines.txt seek=40 bs=1
ddfile if=dd_lines.txt seek=2 skip=1 bs=4 count=3
ddfile if=dd_lines.txt oflag=seek_bytes seek=5
ddfile if=dd_lines.txt oflag=seek_bytes seek=5 conv=notrunc
ddfile if=dd_lines.txt count=0
ddfile if=dd_lines.txt count=0 conv=notrunc
ddfile if=dd_lines.txt bs=1 count=3 seek=1 conv=notrunc
ddfile if=dd_lines.txt conv=excl
ddfile if=dd_lines.txt conv=nocreat
ddfile if=dd_lines.txt conv=fsync
ddfile if=dd_lines.txt conv=ucase obs=7
ddfile if=dd_rand.bin ibs=33 obs=50 count=20
ddfile if=dd_rand.bin skip=3 count=2 ibs=7 obs=11

# options and errors
for o in "if=nosuchfile" "if=" "of=" "bs=x" "bs=1x" "bs=0" "ibs=0" "obs=0" "cbs=0 conv=block" "count=-1" "skip=-1" "seek=-1" "bs=1000000000000000000000" \
         "foo=bar" "foo" "conv=foo" "conv=ucase,lcase" "conv=ascii,ebcdic" "conv=ascii,ibm" "conv=block,unblock cbs=4" "conv=" "iflag=foo" "oflag=foo" \
         "iflag=seek_bytes" "oflag=skip_bytes" "oflag=fullblock" "status=foo" "status=none" "status=noxfer" "status=progress" "count=0x" "bs=2x" "bs=x2" \
         "bs=1xx2" "bs=00x2" "bs=1K" "bs=1KiB" "bs=1MB" "bs=1M" "bs=1w" "bs=1c" "bs=1 " "bs= 1" "bs=+1" "bs=1B" "bs=K" "bs=1kb" "bs=1kk" --bogus \
         "ibs=512 obs=512 bs=3" "skip=1k" "count=1k bs=1" ; do
    eval "compare dd if=dd_lines.txt $o"
done
add_case status C - dd --help
add_case status C - dd --version
ddfile if=dd_lines.txt conv=sparse
ddfile if=dd_all.bin conv=sparse bs=16
ddfile if=dd_zero.bin conv=sparse bs=64
ddfile if=dd_zero.bin conv=sparse,notrunc bs=64
ddfile if=dd_zero.bin conv=sparse bs=64 seek=1
ddfile if=dd_mixzero.bin conv=sparse bs=64
