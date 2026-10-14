# GNU comparison of head: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for f in $TEXTS; do
    for o in "" "-n 3" "-n 0" "-n 100" "-n -2" "-n -100" "-c 7" "-c -5" "-c 0" "-n 1k" "-3" "-c 1k"; do
        compare head $o $f
    done
done
compare head lines.txt words.txt
compare head -n 2 lines.txt words.txt nonl.txt
compare head -q -n 2 lines.txt words.txt
compare head -v -n 2 lines.txt
compare head nosuch.txt
compare head -n x lines.txt
