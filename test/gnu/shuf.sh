# GNU comparison of shuf: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# The same random bytes (--random-source) have to give the same permutation, so the numbers are taken from the bytes
# in the same way; without a source only what does not depend on chance is compared.
for n in 1 2 3 4 5 6; do
    head -c 4000 /dev/urandom > sh_rand$n.bin
done
head -c 4000 /dev/zero > sh_zero.bin
head -c 4000 /dev/zero | tr '\0' '\377' > sh_ff.bin
head -c 4000 /dev/zero | tr '\0' '\200' > sh_80.bin
printf '\001\002\003' > sh_short.bin
seq 1 30 > sh_30.txt
printf 'one\ntwo\nthree\nfour\nfive\nsix' > sh_nonl.txt
printf 'a\0b\0c\0d\0e\0' > sh_nul.txt
printf 'x\r\ny\r\nz\r\n' > sh_crlf.txt
for src in sh_rand1.bin sh_rand2.bin sh_rand3.bin sh_rand4.bin sh_rand5.bin sh_rand6.bin sh_zero.bin sh_ff.bin sh_80.bin; do
    for r in "1-1" "0-0" "1-2" "1-3" "1-5" "1-10" "1-100" "1-1000" "10-30" "0-255" "0-256" "1-65536" "1-70000" \
             "5-1000000" "1-4294967296" "0-18446744073709551614"; do
        compare shuf "--random-source=$src" -i "$r" -n 20
    done
    for r in "1-5" "1-100" "100-110" "1-300"; do
        compare shuf "--random-source=$src" -i "$r"
        compare shuf "--random-source=$src" -i "$r" -r -n 25
        compare shuf "--random-source=$src" -i "$r" -n 3
        compare shuf "--random-source=$src" -i "$r" -n 0
        compare shuf "--random-source=$src" -zi "$r" -n 5
    done
    compare shuf "--random-source=$src" sh_30.txt
    compare shuf "--random-source=$src" sh_30.txt -n 7
    compare shuf "--random-source=$src" -r -n 40 sh_30.txt
    compare shuf "--random-source=$src" sh_nonl.txt
    compare shuf "--random-source=$src" sh_crlf.txt
    compare shuf "--random-source=$src" -z sh_nul.txt
    compare shuf "--random-source=$src" -e a b c d e f g h
    compare shuf "--random-source=$src" -e a b c -n 2
    compare shuf "--random-source=$src" -re x y z -n 12
    compare shuf "--random-source=$src" -n 4 -n 2 sh_30.txt
    compare_stdin sh_30.txt shuf "--random-source=$src"
    compare_stdin sh_30.txt shuf "--random-source=$src" -
    compare_stdin words.txt shuf "--random-source=$src"
done
compare shuf --random-source=sh_short.bin -i 1-10
compare shuf --random-source=sh_short.bin -i 1-3
compare shuf --random-source=sh_short.bin -i 1-300
compare shuf --random-source=sh_short.bin -r -i 1-300 -n 3
compare shuf --random-source=sh_rand1.bin -i 1-1000000000000 -n 5
compare shuf --random-source=sh_rand2.bin -i 1-1000000000000 -n 5 -r
compare shuf --random-source=nosuch.bin -i 1-3
# what does not depend on chance (the sorted result is checked by shuf_test)
compare shuf -n 0 words.txt
compare shuf -i 5-5
compare shuf -i 3-2
compare shuf -e
compare shuf -e only
compare shuf -n 1 -e only
compare shuf -r -n 0 -e a
compare shuf -r -n 3 -e same
compare shuf -r -n 0
compare shuf -r -e
compare shuf -i 1-3 -e a
compare shuf -e -i 1-3
compare shuf -i 1-3 x
compare shuf -i 5-3
compare shuf -i 5
compare shuf -i x-3
compare shuf -i 3-x
compare shuf -i -
compare shuf -i 3-
compare shuf -i 1-3-4
compare shuf -i 1-3 -i 2-4
compare shuf -n x
compare shuf -n -1
compare shuf -n 2k words.txt
compare shuf -n 99999999999999999999 -e z
compare shuf -o a -o b
compare shuf --random-source=a --random-source=b
compare shuf words.txt words.txt
compare shuf nosuch.txt
compare shuf --bogus
add_case status C - shuf --help
add_case status C - shuf --version
