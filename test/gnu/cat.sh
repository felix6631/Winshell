# GNU comparison of cat: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for f in $TEXTS; do
    for o in "" -n -b -s -E -T -v -A -e -t -ns -bs -nE; do
        compare cat $o $f
    done
done
compare cat nosuch.txt lines.txt
compare cat -n lines.txt words.txt
