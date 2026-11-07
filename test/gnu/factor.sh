# GNU comparison of factor: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for n in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 16 17 18 25 27 49 64 97 100 101 121 128 169 210 221 256 343 361 1000 1001 4096 6561 \
         7919 10007 65535 65536 65537 99991 100000 123456 999983 1000000 1000003 4294967291 4294967295 4294967296 4294967297 \
         18446744073709551615 18446744073709551557 18446744073709551556 9223372036854775807 9223372036854775808 \
         600851475143 1234567890123456789 1000000007 2147483647 2147483649 3037000499 3037000501 9999999967 \
         1000000016000000063 4611686014132420609 18446744030759878681 +7 007 00 0001; do
    compare factor "$n"
done
compare factor 12 15 21
compare factor 12 abc 15
compare factor
compare factor 1.5
compare factor 1e3
compare factor 0x10
compare factor ''
compare factor -- 12
compare factor --bogus
printf '12 15\n21\n\t7  8\n' > fac_in.txt
printf '12 abc 15\n' > fac_bad.txt
printf '' > fac_empty.txt
printf '97' > fac_nonl.txt
for f in fac_in.txt fac_bad.txt fac_empty.txt fac_nonl.txt; do
    compare_stdin "$f" factor
done
add_case status C - factor --help
add_case status C - factor --version
