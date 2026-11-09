# GNU comparison of od: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# (-t fL / f16, the 80-bit long double, is not provided here and is left out)
# All the types and sizes on bytes that give all kinds of numbers; the widths, the skipping, the strings and the errors.
printf '\001\002\377\200\177abc\n\000\376\325\252\125\012\015\t\\' > od_mix.bin
printf 'abc' > od_3.bin
printf 'abcdefg' > od_7.bin
head -c 100 /dev/zero > od_zero.bin
printf 'aaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' > od_dup.bin
head -c 700 /dev/urandom > od_rand.bin
printf 'abc\0defgh\0ij\0kkkk\0\001\002long string here\0tab\there\0end' > od_str.bin
printf '\000\000\200\077\000\000\200\177\000\000\200\377\000\000\300\177\001\000\000\000\377\377\177\177\000\000\000\000\000\000\000\200' > od_float.bin
printf '\000\000\000\000\000\000\360\077\000\000\000\000\000\000\360\177\000\000\000\000\000\000\360\377\000\000\000\000\000\000\370\177\030\055\104\124\373\041\011\100' > od_double.bin
for f in od_mix.bin od_3.bin od_7.bin od_zero.bin od_dup.bin od_rand.bin od_float.bin od_double.bin empty.txt words.txt; do
    for t in a c o1 o2 o4 o8 d1 d2 d4 d8 u1 u2 u4 u8 x1 x2 x4 x8 f4 f8 fF fD oC oS oI oL dC dS dI dL xL uS a z c z x1z d2z o4z f4z f8z \
             "x1 -t o1" "x2 -t c" "x1 -t d2" "d1 -t x2" "o1 -t c -t d2" "a -t x4" "f4 -t x1" "u1 -t u2 -t u4 -t u8" "x8 -t x1" "c -t o2z" \
             "x1d2" "o1x1" "d4u2" ; do
        compare od -t $t $f
    done
    for o in "" -b -c -d -f -i -l -o -s -x -a -h -bc -ac -cx -dx -ox -fx -v "-v -c" "-A d" "-A o" "-A x" "-A n" "-A n -c" "-A x -t x1" \
             "-w1" "-w2" "-w4" "-w8" "-w16" "-w32" "-w" "-w3" "-w5 -t x1" "-w6 -t x2" "-w3 -t x1" "-w12 -t x4" "-w4 -t x1 -t x2" "-w8 -v -t x1" \
             "-j 1" "-j 3 -t x1" "-j 7" "-j 100" "-j 0x2" "-j 1b" "-j 1k" "-N 1" "-N 5 -t x1" "-N 0" "-N 1000" "-j 2 -N 3 -t x1" \
             "-j 3 -N 100 -c" "-A d -j 5 -N 9 -t x1" "--endian=big" "--endian=little" "--endian=big -t x4" "--endian=big -t d2" \
             "--endian=big -t f4" "--endian=big -t u8" "--endian=big -t c" "--traditional" "-v -w16 -t u1 -A d" "-t x1z -w8" ; do
        eval "compare od $o $f"
    done
done
compare_stdin od_mix.bin od
compare_stdin od_mix.bin od -c
compare_stdin od_mix.bin od -A x -t x1
compare od od_3.bin od_7.bin
compare od -t x1 od_3.bin od_7.bin
compare od -t x2 od_3.bin od_7.bin
compare od -c -j 2 od_3.bin od_7.bin
compare od -c -j 5 od_3.bin od_7.bin
compare od -c -j 10 od_3.bin od_7.bin
compare od -c -j 11 od_3.bin od_7.bin
compare od -t x1 od_3.bin nosuch.bin od_7.bin
compare od -t x1 nosuch.bin
compare od -t x1 .
compare od -t x1 - od_3.bin
# strings
for f in od_str.bin od_mix.bin od_rand.bin od_7.bin empty.txt words.txt; do
    for o in "-S 3" "-S 4" "-S 1" "-S 2" "-S 10" "--strings" "--strings=5" "--strings=3" "-A x -S 3" "-A n -S 3" "-A d -S 3" "-S 3 -j 2" "-S 3 -N 8" \
             "-S 3 -N 20" "-S 3 -j 4 -N 30" "-S 3 -t x1" "-S 4 -v"; do
        eval "compare od $o $f"
    done
done
# options and errors
for o in "-t d3" "-t d0" "-t d16" "-t f3" "-t q" "-t ''" "-t c2" "-t a2" "-t dz" "-t zd" "-t x1zz" "-t dX" "-t fC" "-t xF" \
         "-t fL1" "-t x1d3" "-t d3x1" "-t x1fq" "-t x1 -t" "-A q" "-A" "-A dd" "-A od" "-j x" "-j -1" "-j" "-N x" "-N -1" "-N 1k" "-N 1kB" \
         "-N 1KiB" "-N 1b" "-j 0x" "-j 011" "-j 08" "-w0" "-w x" "--width=x" "--width=5 -t x1" "--endian=x" "--endian" "-S" "-S x" "-S 0" \
         "--strings=0" "--strings=x" "--bogus" "-z" "-j 3 -j 5" "-N 2 -N 5" "-bcd" "-ab" "-t x1 -s" "-t u1x2" "-t x2 -t x1 -t x4" \
         "-t c -t a" "-c -c" "-t x1 -t x1" "-w2 -t x1 -t x2" "-t a -t c --width=2" "-v -w1" "-t x1 -w2 -t d2" ; do
    eval "compare od $o od_7.bin"
done
add_case status C - od --help
add_case status C - od --version
compare od -S 0 od_str.bin
compare od --strings=0 od_str.bin
compare od -S 1 od_str.bin
