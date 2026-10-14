# GNU comparison of cut: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for f in table.txt tabs.txt words.txt empty.txt nonl.txt big.txt; do
    for o in "-c1" "-c1-3" "-c2-" "-c-2" "-c1,3" "-c3,1" "-b1-2,4" "-b 2-3 --complement" "-f1" "-f2" "-f1,3" "-f2-" \
             "-f-2" "-f1 -s" "-f2 --complement" "-d: -f1" "-d: -f2,3" "-d: -f2 -s" "-d: -f1,3 --output-delimiter=@" \
             "-d: -f 1- --output-delimiter=" "-c1-2,4-5 --output-delimiter=|" "-b1,2 --output-delimiter=-"; do
        compare cut $o $f
    done
done
compare cut -f1 nosuch.txt
compare cut words.txt
compare cut -f0 table.txt
compare cut -c1 -f1 table.txt
compare cut -f1 -d:: table.txt
