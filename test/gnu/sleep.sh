# GNU comparison of sleep: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for n in 0 0.001 0s 0m 0.0001h 0.00001d 1e-3 .001 0.001s0 "0 0" "0.001 0.001s" abc 1x -1 "" 1.2.3 0x1 inf2 "0 abc" 1ss; do
    compare sleep "$n"
done
compare sleep
compare sleep --bogus
