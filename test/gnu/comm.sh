# GNU comparison of comm: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
printf 'a\nb\nc\nd\ne\n' > cmA.txt
printf 'b\nc\nx\ny\n' > cmB.txt
printf 'a\na\nb\nb\nc\n' > cmDupA.txt
printf 'a\nb\nb\nb\nd\n' > cmDupB.txt
printf 'c\nb\na\n' > cmUnsorted.txt
printf 'a\nc\nb\n' > cmUnsorted2.txt
printf 'a\nb\nlast' > cmNoNl.txt
printf 'b\nz\n' > cmZ.txt
printf 'a\0b\0d\0' > cmNul1.txt
printf 'b\0c\0d\0' > cmNul2.txt
for pair in "cmA.txt cmB.txt" "cmB.txt cmA.txt" "cmDupA.txt cmDupB.txt" "cmA.txt cmA.txt" "empty.txt cmA.txt" "cmA.txt empty.txt" \
            "empty.txt empty.txt" "cmUnsorted.txt cmA.txt" "cmA.txt cmUnsorted.txt" "cmUnsorted.txt cmUnsorted2.txt" \
            "cmNoNl.txt cmZ.txt" "cmZ.txt cmNoNl.txt" "cmNoNl.txt cmNoNl.txt"; do
    for o in "" "-1" "-2" "-3" "-12" "-13" "-23" "-123" "--total" "--check-order" "--nocheck-order" "--output-delimiter=:" \
             "--output-delimiter=,, -3" "-1 --total '--output-delimiter=|'"; do
        eval "set -- $o $pair"
        add_case out C - comm "$@"
    done
done
add_case out C - comm -z cmNul1.txt cmNul2.txt
add_case out C - comm -z --total cmNul1.txt cmNul2.txt
add_case out C cmB.txt comm cmA.txt -
add_case out C cmA.txt comm - cmB.txt
add_case status C - comm cmA.txt nosuch.txt
add_case status C - comm nosuch.txt cmA.txt
add_case status C - comm cmA.txt
add_case status C - comm
add_case status C - comm cmA.txt cmB.txt cmA.txt
add_case status C - comm --bogus cmA.txt cmB.txt
add_case status C - comm --output-delimiter=a --output-delimiter=b cmA.txt cmB.txt
