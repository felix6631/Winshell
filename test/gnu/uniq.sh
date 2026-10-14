# GNU comparison of uniq: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for f in dup.txt words.txt fields.txt empty.txt nonl.txt blank.txt; do
    for o in "" -c -d -D -u -i -ci "-f1" "-f 1 -c" "-s 2" "-w 1" "-w 2 -c" "-d -c" "-u -c" "-i -d"; do
        compare uniq $o $f
    done
done
compare_stdin dup.txt uniq -c
