# GNU comparison of tee: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# tee writes files, so these are run one after the other and at once
for f in words.txt nonl.txt empty.txt big.txt; do
    for o in "" -a; do
        total=$((total + 1))
        rm -f exp.f1 exp.f2 got.f1 got.f2
        printf 'old\n' > exp.f1; cp exp.f1 got.f1
        gnu tee $o exp.f1 exp.f2 < $f > exp.out 2> exp.err; erc=$?
        ours tee $o got.f1 got.f2 < $f > got.out 2> got.err; grc=$?
        if ! cmp -s exp.out got.out || ! cmp -s exp.f1 got.f1 || ! cmp -s exp.f2 got.f2 || [ $erc != $grc ]; then
            bad=$((bad + 1))
            echo "DIFFERS: tee $o < $f (status $erc, ours $grc)"
        fi
    done
done
total=$((total + 1))
gnu tee < words.txt > exp.out 2> exp.err; erc=$?
ours tee < words.txt > got.out 2> got.err; grc=$?
if ! cmp -s exp.out got.out || [ $erc != $grc ]; then
    bad=$((bad + 1))
    echo "DIFFERS: tee (no file)"
fi
