# GNU comparison of tail: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for f in $TEXTS; do
    for o in "" "-n 3" "-n 0" "-n 100" "-n +3" "-n +1" "-n +100" "-c 7" "-c -5" "-c +4" "-c +1000" "-c 3b" "-3"; do
        compare tail $o $f
    done
done
compare tail -n 2 lines.txt words.txt
compare tail -q -n 2 lines.txt words.txt
compare tail -v -n 2 lines.txt
compare tail nosuch.txt
