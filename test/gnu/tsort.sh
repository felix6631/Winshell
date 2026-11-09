# GNU comparison of tsort: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# Graphs with and without loops; the loops go to standard error (not compared) and decide what is dropped,
# so the output has to agree on every one of the random graphs.
printf 'a b\nb c\nc d\n' > ts_chain.txt
printf 'd c\nc b\nb a\n' > ts_back.txt
printf 'a b\na c\nb d\nc d\n' > ts_diamond.txt
printf 'a a\nb b\n' > ts_self.txt
printf 'a b b a\n' > ts_loop2.txt
printf 'a b b c c a\n' > ts_loop3.txt
printf 'a b\nb c\nc a\nd a\n' > ts_loop3d.txt
printf 'a b b c c d d b a z\n' > ts_loop3z.txt
printf 'c b b a a c a b\n' > ts_loop3c.txt
printf 'b c c b b a\n' > ts_tail.txt
printf 'a b b c c a a c c b b a\n' > ts_two.txt
printf 'a b a b a b\n' > ts_dups.txt
printf '  a\t b \n\n c\n\n  d  ' > ts_blanks.txt
printf 'a b c\n' > ts_odd.txt
printf 'apple banana\nbanana cherry\nApple Banana\n10 9\n9 8\n' > ts_words.txt
for f in ts_chain.txt ts_back.txt ts_diamond.txt ts_self.txt ts_loop2.txt ts_loop3.txt ts_loop3d.txt ts_loop3z.txt \
         ts_loop3c.txt ts_tail.txt ts_two.txt ts_dups.txt ts_blanks.txt ts_odd.txt ts_words.txt empty.txt words.txt; do
    compare tsort "$f"
    compare_stdin "$f" tsort
    compare_stdin "$f" tsort -
done
RANDOM=7
for n in $(seq 1 120); do
    nodes=$((2 + RANDOM % 7))
    pairs=$((1 + RANDOM % 14))
    : > "ts_r$n.txt"
    for p in $(seq 1 $pairs); do
        printf '%s %s\n' "n$((RANDOM % nodes))" "n$((RANDOM % nodes))" >> "ts_r$n.txt"
    done
    compare tsort "ts_r$n.txt"
done
for n in $(seq 1 60); do
    nodes=$((8 + RANDOM % 20))
    pairs=$((10 + RANDOM % 40))
    : > "ts_s$n.txt"
    for p in $(seq 1 $pairs); do
        printf '%s %s\n' "$((RANDOM % nodes))" "$((RANDOM % nodes))" >> "ts_s$n.txt"
    done
    compare tsort "ts_s$n.txt"
done
compare tsort nosuchfile
compare tsort ts_chain.txt ts_chain.txt
compare tsort --bogus
add_case status C - tsort --help
add_case status C - tsort --version
