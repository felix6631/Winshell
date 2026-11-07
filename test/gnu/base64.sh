# GNU comparison of base64 (and base32 in base32.sh): sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
printf 'a' > b_a.txt
printf 'ab' > b_ab.txt
printf 'abc' > b_abc.txt
printf 'abcd' > b_abcd.txt
printf '\000\001\377\376binary\n' > b_bin.txt
seq 1 200 | tr '\n' ' ' > b_long.txt
for f in b_a.txt b_ab.txt b_abc.txt b_abcd.txt b_bin.txt b_long.txt words.txt nonl.txt empty.txt tabs.txt blank.txt big.txt; do
    compare_stdin "$f" base64
    for w in 0 1 4 5 76 100; do
        compare_stdin "$f" base64 -w "$w"
    done
    compare base64 "$f"
    compare base64 --wrap=10 "$f"
done
# decoding: the encodings above, one broken at a time
for enc in YQ== YWI= YWJj YWJjZA== YWJjZGU= '' YQ YQ= Y YWJjZA YQ==YQ== 'YQ==
YWI=
' 'YW Jj' 'YWJj!' '!!!!' = == '=Q==' 'YQ=a' 'YQ==a' 'YQ==  ' 'YQ ==' 'AAAA' '////' '++++' 'YWJjZGVmZ2g=' 'YWJjZGVmZ2g' 'Y W J j' 'YWJjZGVm
ZGhp' 'Zm9v
YmFy
'; do
    printf '%s' "$enc" > b_enc.txt
    compare_stdin b_enc.txt base64 -d
    compare_stdin b_enc.txt base64 --decode
    compare_stdin b_enc.txt base64 -di
    compare_stdin b_enc.txt base64 -d -w 4
done
# the other way round for the binary and long files
for f in b_bin.txt b_long.txt words.txt big.txt; do
    printf '%s\n' "$f" > /dev/null
done
compare base64 nosuchfile
compare base64 a b
compare base64 -d nosuchfile
compare base64 -w
compare base64 -w x words.txt
compare base64 -w -1 words.txt
compare base64 --bogus
compare base64 -x
add_case status C - base64 --help
add_case status C - base64 --version
