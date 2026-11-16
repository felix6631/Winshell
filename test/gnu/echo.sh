# GNU comparison of echo: the words, -n, -e, -E and every escape of -e. (--help and --version only count when they are the
# one argument.)
for a in "" a "a b" "a  b" -n "-n a" "-n -n a" -e -E "-e a" "-nE a" "-neE a" "-Een a" -x "-n -x" -- "-- a" "a --" "-n --"; do
    compare echo $a
done
compare echo ''
compare echo '' ''
compare echo -n ''
compare echo -e 'a\tb'
compare echo -e 'a\nb'
compare echo -e 'a\b'
compare echo -e 'a\0101b'
compare echo -e 'a\x41b'
compare echo -e 'a\x4'
compare echo -e 'a\xzz'
compare echo -e 'a\cb'
compare echo -e 'a\c'
compare echo -e '\a\b\f\r\v\e' 
compare echo -e 'a\qb'
compare echo -e 'a\'
compare echo -e '\0'
compare echo -e '\08'
compare echo -e '\1'
compare echo -E 'a\tb'
compare echo -n -e 'a\n'
compare echo -ne 'a\n'
compare echo -en 'a\n'
compare echo 'a\tb'
compare echo -e 'a\x41\x4a\x4B'
compare echo -e 'A'
compare echo '$HOME'
compare echo '*'
compare echo a '' b
compare echo -e 'x\0y'
add_case status C - echo --help
add_case status C - echo --version
compare echo --version a
compare echo a --version
compare echo --help a
