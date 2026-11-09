# GNU comparison of join: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# (CR LF input is meant to differ: GNU keeps the CR in the last field; join_test covers it)
printf 'a 1\nb 2\nc 3\n' > jn_a.txt
printf 'a x\nc y\nd z\n' > jn_b.txt
printf 'a 1\na 2\nb 3\nb 4\n' > jn_dupa.txt
printf 'a x\na y\nb z\nc w\n' > jn_dupb.txt
printf 'b 1\na 2\n' > jn_unsorted.txt
printf 'a 1\nB 2\nc 3\n' > jn_case1.txt
printf 'A x\nb y\nC z\n' > jn_case2.txt
printf 'a,1,2\nb,3,4\nc,,6\n' > jn_c1.txt
printf 'a,x\nb,y\nd,\n' > jn_c2.txt
printf 'k v\na 1\nb 2\n' > jn_h1.txt
printf 'k w\na 3\nc 4\n' > jn_h2.txt
printf 'a b \nc  d\n  e f\n' > jn_blanks.txt
printf 'a\t1\tx\nb\t2\ty\n' > jn_tab1.txt
printf 'a\tp\nb\tq\n' > jn_tab2.txt
printf 'a 1\nb 2\nc' > jn_nonl.txt
printf 'a\0b c\0' > jn_z1.txt
printf 'a\0b d\0' > jn_z2.txt
printf '1 a\n2 b\n3 c\n' > jn_n1.txt
printf '2 x\n3 y\n4 z\n' > jn_n2.txt
printf '\n\n' > jn_lines.txt
: > jn_empty.txt
for pair in "jn_a.txt jn_b.txt" "jn_b.txt jn_a.txt" "jn_dupa.txt jn_dupb.txt" "jn_a.txt jn_a.txt" "jn_a.txt jn_empty.txt" \
            "jn_empty.txt jn_b.txt" "jn_empty.txt jn_empty.txt" "jn_unsorted.txt jn_a.txt" "jn_a.txt jn_unsorted.txt" \
            "jn_unsorted.txt jn_unsorted.txt" "jn_case1.txt jn_case2.txt" "jn_h1.txt jn_h2.txt" "jn_blanks.txt jn_blanks.txt" \
            "jn_nonl.txt jn_a.txt" "jn_a.txt jn_nonl.txt" "jn_n1.txt jn_n2.txt" \
            "jn_lines.txt jn_lines.txt" "jn_lines.txt jn_a.txt"; do
    for o in "" "-a1" "-a2" "-a1 -a2" "-v1" "-v2" "-v1 -v2" "-a1 -v2" "-e X -a1 -a2" "-i" "-i -a1" "--check-order" "--nocheck-order" \
             "--header" "--header -a1 -a2" "-o 0,1.2,2.2" "-o 0,1.2,2.2 -e X -a1 -a2" "-o 1.1,1.2,2.1,2.2 -a1 -a2" "-o auto" \
             "-o auto -a1 -a2 -e ." "-o 2.2,0,1.2" "-o '1.2 2.2'" "-o 1.9,2.9 -e Y -a1" "-e '' -a1 -a2" "-t :" "-t ''" \
             "-1 2 -2 1" "-1 2 -2 2" "-j 2" "-j 1 -a1" "-z" "-t x -i"; do
        compare_words join "$o $pair"
    done
done
for pair in "jn_c1.txt jn_c2.txt" "jn_c2.txt jn_c1.txt"; do
    for o in "-t ," "-t , -a1" "-t , -a2 -e X" "-t , -a1 -a2 -o 0,1.2,1.3,2.2" "-t , -o auto -a1 -a2" "-t , -e '' -o 1.3,2.2 -a1" \
             "-t , -1 2 -2 1" "-t , -j 3 -a1" "-t , -v1" "-t , -v2 -o 0,2.2" ; do
        compare_words join "$o $pair"
    done
done
compare_words join "-t '\\0' jn_z1.txt jn_z2.txt"
compare_words join "-z jn_z1.txt jn_z2.txt"
compare_words join "-z -a1 -a2 jn_z1.txt jn_z2.txt"
compare_words join "-z -t ' ' jn_z1.txt jn_z2.txt"
compare_words join "-t \"\$(printf '\\t')\" jn_tab1.txt jn_tab2.txt"
compare_words join "-t \"\$(printf '\\t')\" -a1 -o 0,1.3,2.2 jn_tab1.txt jn_tab2.txt"
compare_words join "-t \"\$(printf '\\t')\" -a2 -e - -o 0,1.3,2.2 jn_tab1.txt jn_tab2.txt"
compare_stdin jn_b.txt join - jn_a.txt
compare_stdin jn_b.txt join jn_a.txt -
compare_stdin jn_a.txt join - jn_a.txt
compare join - -
compare join jn_a.txt nosuchfile
compare join nosuchfile jn_a.txt
compare join jn_a.txt
compare join
compare join jn_a.txt jn_b.txt jn_a.txt
# options and their errors
for o in "-j 0" "-1 0" "-2 0" "-1 x" "-j x" "-a 3" "-a 0" "-a x" "-v 3" "-o 1" "-o 0.1" "-o 00" "-o 3.1" "-o x" "-o 1.x" "-o 1.0" \
         "-o ''" "-o ,"  "-o '1.1,'" "-o '1.1  2.1'" "-o 1.1,2.1" "-o '1.01'" "-o 1.99999999999999999999" "-t ab" "-t , -t ';'" \
         "-t , -t ,"  "-e A -e B" "-e A -e A -a1" "-o auto -o 0" "-o 0 -o auto" "-o 0 -o 1.1" "-j 99999999999999999999" "-1 +2" \
         "--bogus" "-x"; do
    compare_words join "$o jn_a.txt jn_b.txt"
done
add_case status C - join --help
add_case status C - join --version
# random sorted inputs: keys from a small alphabet, sorted as join expects (C locale), a few fields
RANDOM=11
for n in $(seq 1 60); do
    : > "jn_r${n}a.txt"; : > "jn_r${n}b.txt"
    lines=$((RANDOM % 9))
    for i in $(seq 1 $lines); do printf '%s %s %s\n' "k$((RANDOM % 5))" "a$i" "$((RANDOM % 3))" >> "jn_r${n}a.txt"; done
    lines=$((RANDOM % 9))
    for i in $(seq 1 $lines); do printf '%s %s\n' "k$((RANDOM % 5))" "b$i" >> "jn_r${n}b.txt"; done
    LC_ALL=C sort -s -k1,1 "jn_r${n}a.txt" -o "jn_r${n}a.txt"
    LC_ALL=C sort -s -k1,1 "jn_r${n}b.txt" -o "jn_r${n}b.txt"
    for o in "" "-a1 -a2" "-v1" "-v2" "-v1 -v2" "-o auto -a1 -a2 -e -" "-o 0,1.2,2.2 -a1 -e Z" "--header -a1"; do
        compare_words join "$o jn_r${n}a.txt jn_r${n}b.txt"
    done
done
# random unsorted inputs: where the order is reported and what is still joined
for n in $(seq 1 40); do
    : > "jn_u${n}a.txt"; : > "jn_u${n}b.txt"
    lines=$((1 + RANDOM % 8))
    for i in $(seq 1 $lines); do printf '%s %s\n' "k$((RANDOM % 4))" "a$i" >> "jn_u${n}a.txt"; done
    lines=$((1 + RANDOM % 8))
    for i in $(seq 1 $lines); do printf '%s %s\n' "k$((RANDOM % 4))" "b$i" >> "jn_u${n}b.txt"; done
    for o in "" "-a1 -a2" "--nocheck-order" "--check-order" "-v1"; do
        compare_words join "$o jn_u${n}a.txt jn_u${n}b.txt"
    done
done
