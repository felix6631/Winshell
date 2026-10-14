# GNU comparison of tac: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for f in $TEXTS sections.txt; do
    for o in "" -b "-s :" "-b -s :" "-s ab" "-bs ab" "-s ''"; do
        compare_words tac "$o $f"
    done
done
compare tac lines.txt words.txt
compare tac nosuch.txt lines.txt
compare_stdin words.txt tac
compare_stdin nonl.txt tac -s a
