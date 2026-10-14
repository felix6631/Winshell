# GNU comparison of wc: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for f in $TEXTS; do
    for o in "" -l -w -c -m -L -lw -lwc -lwcmL -cl; do
        compare wc $o $f
    done
done
compare wc lines.txt words.txt
compare wc -l lines.txt words.txt nonl.txt
compare wc -c lines.txt big.txt
compare wc nosuch.txt lines.txt
compare_stdin lines.txt wc
compare_stdin lines.txt wc -l
