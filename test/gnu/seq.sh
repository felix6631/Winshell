# GNU comparison of seq: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for last in 1 5 0 -3 10 100; do
    compare seq "$last"
    compare seq -w "$last"
    compare seq -s, "$last"
done
for first in 1 -2 3 0 7 -5; do
    for last in 5 -3 0 3 10; do
        compare seq "$first" "$last"
        compare seq -w "$first" "$last"
        compare seq -s' ' "$first" "$last"
        for step in 1 2 -1 -2 3 5; do
            compare seq "$first" "$step" "$last"
            compare seq -w "$first" "$step" "$last"
        done
    done
done
# fractions: the number of decimals comes from FIRST and INCREMENT
for args in "0.5 3" "1 0.5 3" "1 0.25 2" "0.1 0.1 0.5" "1.5 4" "2 -0.5 0" "-1 0.5 1" "1.0 3" "1 1.50 4" "0.001 0.001 0.004" "5 .5 7" "1 0.3 2"; do
    compare_words seq "$args"
    compare_words seq "-w $args"
    compare_words seq "-s: $args"
done
# formats
compare seq -f '%g' 1 3
compare seq -f '%.2f' 1 3
compare seq -f 'x%gy' 1 3
compare seq -f '%5.1f' 1 0.5 3
compare seq -f '%e' 1 3
compare seq -f '%03g' 1 3
compare seq -f '%%%g' 1 3
compare seq -f '%g' 3
compare seq --format='%.1f' 1 2
compare seq --format '%.1f' 1 2
compare seq -f '%d' 1 3
compare seq -f 'none' 1 3
compare seq -f '%g %g' 1 3
compare seq -f '%g' -w 1 3
compare seq -f '%s' 1 3
# separators and options
compare seq -s '' 1 3
compare seq -s ab 1 3
compare seq --separator=, 1 3
compare seq --separator , 1 3
compare seq --equal-width 8 11
compare seq -ws, 8 11
compare seq -w -s , 8 11
compare seq -- 1 3
compare seq -- -3 -1
compare seq 3 1
compare seq 1 -1 3
compare seq 5 5
compare seq 1 1 1
compare seq 0x10
compare seq 0x2 0x5
compare seq 1e2 1e2
compare seq 1 1e1
# errors
compare seq
compare seq 1 2 3 4
compare seq a
compare seq 1 a
compare seq 1 0 3
compare seq 1 0.0 3
compare seq 1 2 b
compare seq ''
compare seq nan
compare seq 1 nan
compare seq --bogus 3
compare seq -x 3
compare seq -f
compare seq -s
add_case status C - seq --help
add_case status C - seq --version
