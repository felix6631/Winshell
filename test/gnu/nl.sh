# GNU comparison of nl: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for f in $TEXTS sections.txt; do
    for o in "" -ba -bn -ba\ -l2 -ba\ -l3 -n\ ln -n\ rz -n\ rn -w3 -w10 -s: -s\ -- "-v 10 -i 5" "-v -3" -i-2 -p "-ha -fa" "-ba -ha -fa" "-ba -p" "-d xy -ba" "-d x" "-nrz -w 3 -v 98 -ba"; do
        compare_words nl "$o $f"
    done
done
compare nl words.txt lines.txt
compare nl -ba nonl.txt words.txt
compare nl nosuch.txt lines.txt
compare nl -w0 lines.txt
compare nl -l0 lines.txt
compare nl -n xx lines.txt
compare nl -b x lines.txt
compare_stdin words.txt nl -ba
