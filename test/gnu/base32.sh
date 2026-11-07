# GNU comparison of base32: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
printf 'a' > b_a.txt
printf 'ab' > b_ab.txt
printf 'abc' > b_abc.txt
printf 'abcd' > b_abcd.txt
printf 'abcde' > b_abcde.txt
printf '\000\001\377\376binary\n' > b_bin.txt
seq 1 200 | tr '\n' ' ' > b_long.txt
for f in b_a.txt b_ab.txt b_abc.txt b_abcd.txt b_abcde.txt b_bin.txt b_long.txt words.txt nonl.txt empty.txt tabs.txt blank.txt big.txt; do
    compare_stdin "$f" base32
    for w in 0 1 4 8 76 100; do
        compare_stdin "$f" base32 -w "$w"
    done
    compare base32 "$f"
    compare base32 --wrap=10 "$f"
done
# decoding: the encodings above, one broken at a time
for enc in ME====== MFRA==== MFRGG=== MFRGGZA= MFRGGZDF '' ME MFRGG M MFRGGZ== MFRGGZDF======== ME======MFRA==== 'ME======
MFRA====
' 'ME ======' 'ME======!' '!!!!!!!!' = ======== 'M=======' 'ME=====A' 'ME======A' AAAAAAAA 77777777 mfrgg=== 'ME======  ' 'M E======' MFRGGZDFMFRGGZDF MFRGGZDFMFRGGZDF====; do
    printf '%s' "$enc" > b_enc.txt
    compare_stdin b_enc.txt base32 -d
    compare_stdin b_enc.txt base32 --decode
    compare_stdin b_enc.txt base32 -di
    compare_stdin b_enc.txt base32 -d -w 4
done
compare base32 nosuchfile
compare base32 a b
compare base32 -d nosuchfile
compare base32 -w
compare base32 -w x words.txt
compare base32 -w -1 words.txt
compare base32 --bogus
compare base32 -x
add_case status C - base32 --help
add_case status C - base32 --version
