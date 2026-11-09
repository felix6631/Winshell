# GNU comparison of b2sum: the cases of the other sum tools are in _sums.sh; here the digest length (-l) and the
# checking of sums of different lengths.
[ "$(type -t sum_cases)" = function ] || . "$HERE/gnu/_sums.sh"
sum_cases b2sum
for l in 8 16 64 128 256 384 504 512 0; do
    for f in words.txt empty.txt nonl.txt big.txt; do
        compare b2sum -l $l "$f"
        compare b2sum -l $l --tag "$f"
        compare b2sum --length=$l -z "$f"
    done
    compare b2sum -l $l words.txt nonl.txt
done
for l in 7 9 100 513 520 1000 -8 +8 x "" 8x 1.5; do
    compare b2sum -l "$l" words.txt
    compare b2sum -l "$l" -c words.txt
done
# checking: sums of several lengths in one file, tagged or not
for l in 8 64 128 256 512; do
    gnu b2sum -l $l words.txt > "b_l$l.txt" 2>/dev/null
    gnu b2sum -l $l --tag words.txt nonl.txt > "b_t$l.txt" 2>/dev/null
    compare b2sum -c "b_l$l.txt"
    compare b2sum -c "b_t$l.txt"
    compare b2sum -l 64 -c "b_l$l.txt"
    compare b2sum -l 256 -c "b_t$l.txt"
done
cat b_l8.txt b_l64.txt b_t256.txt b_l512.txt b_t512.txt > b_all.txt
sed '1s/^./0/' b_all.txt > b_allbad.txt
compare b2sum -c b_all.txt
compare b2sum -c b_allbad.txt
compare b2sum -c --quiet b_allbad.txt
compare b2sum -c --status b_allbad.txt
h=$(gnu b2sum -l 64 words.txt | cut -d' ' -f1)
H=$(gnu b2sum words.txt | cut -d' ' -f1)
printf '%s *words.txt\n' "${h}0" > b_odd.txt
printf '%s *words.txt\n' "${H}00" > b_long.txt
printf '%s *words.txt\n' "" > b_none.txt
printf 'BLAKE2b-64 (words.txt) = %s\n' "$H" > b_taglong.txt
printf 'BLAKE2b (words.txt) = %s\n' "$h" > b_tagshort.txt
printf 'BLAKE2b-0 (words.txt) = %s\n' "$h" > b_tag0.txt
printf 'BLAKE2b-7 (words.txt) = %s\n' "$h" > b_tag7.txt
printf 'BLAKE2b-520 (words.txt) = %s00\n' "$H" > b_tag520.txt
printf 'BLAKE2b-x (words.txt) = %s\n' "$h" > b_tagx.txt
printf 'BLAKE2 (words.txt) = %s\n' "$H" > b_tagshortname.txt
printf 'SHA512 (words.txt) = %s\n' "$H" > b_tagsha.txt
printf '%s  words.txt\n' "$(echo $h | tr a-f A-F)" > b_upper.txt
for c in b_odd.txt b_long.txt b_none.txt b_taglong.txt b_tagshort.txt b_tag0.txt b_tag7.txt b_tag520.txt b_tagx.txt \
         b_tagshortname.txt b_tagsha.txt b_upper.txt; do
    compare b2sum -c "$c"
    compare b2sum -c -w "$c"
    compare b2sum -c --strict "$c"
done
compare b2sum --tag -c words.txt
compare b2sum -l 64 --tag -t words.txt
add_case status C - b2sum --help
add_case status C - b2sum --version
