# GNU comparison of tr: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for args in "a-z A-Z" "[:lower:] [:upper:]" "[:upper:] [:lower:]" "abc xyz" "abc x" "a-c 12" "-d a-c" "-d [:digit:]" \
            "-d [:space:]" "-s a-z" "-s ' '" "-c a-z _" "-cd a-z" "-cs a-z \\n" "-ds a-c d" "-s a-b A-B" \
            "-t abcd xy" "a-c [x*]" "abc [x*2]" "\\n \\t" "[=a=] b" "a-b -" "-d \\n"; do
    for f in words.txt table.txt; do
        compare_words tr "$args" $f
    done
done
# the errors: only the status is compared
for args in "" "a" "-d a b" "z-a x" "[:foo:] x" "a-c [x*] y" "[a*] b"; do
    status_words tr "$args" words.txt
done
