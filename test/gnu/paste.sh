# GNU comparison of paste: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for f in $TEXTS; do
    for o in "" -s "-d :" "-d ',;'" "-s -d ',;'" "-d '\n'" "-d '\0'" "-d '\t\\'" "-s -d '\0'" -z; do
        compare_words paste "$o $f"
    done
done
for o in "" -s "-d ," "-s -d ,:" "-d ''" "-s -d ''" "-d '\'" "-d ab"; do
    for files in "words.txt lines.txt" "lines.txt words.txt dup.txt" "lines.txt empty.txt words.txt" "empty.txt" "nonl.txt lines.txt" "lines.txt lines.txt" "blank.txt dup.txt"; do
        compare_words paste "$o $files"
    done
done
compare paste nosuch.txt lines.txt
# the same standard input twice takes its lines in turn
for o in "" "-s" "-d ,"; do
    for f in lines.txt words.txt nonl.txt; do
        compare_words paste "$o - -" $f
    done
done
compare_stdin lines.txt paste - words.txt -
