# GNU comparison of basenc: every encoding encoded, wrapped and decoded, with broken input (what is written before the
# error and the status), -i, and the checks of the options.
printf '' > bn_empty.txt
printf 'a' > bn_a.txt
printf 'ab' > bn_ab.txt
printf 'abcd' > bn_abcd.txt
printf 'abcdefgh' > bn_8.txt
printf 'abcdefghijkl' > bn_12.txt
printf '\000\001\377\376binary\n' > bn_bin.txt
seq 1 200 | tr '\n' ' ' > bn_long.txt
for e in --base64 --base64url --base32 --base32hex --base16 --base2msbf --base2lsbf --z85; do
    for f in bn_empty.txt bn_a.txt bn_ab.txt bn_abcd.txt bn_8.txt bn_12.txt bn_bin.txt bn_long.txt words.txt nonl.txt tabs.txt; do
        compare_stdin "$f" basenc $e
        compare basenc $e "$f"
        for w in 0 1 4 5 76 100; do
            compare_stdin "$f" basenc $e -w "$w"
        done
        compare_stdin "$f" basenc $e --wrap=10
    done
done
bn_n=0
bn_next() { bn_n=$((bn_n + 1)); BN=bn_enc_$bn_n.txt; }
# decoding: valid text, then broken text one way at a time
for e in --base64 --base64url --base32 --base32hex --base16 --base2msbf --base2lsbf --z85; do
    for f in bn_a.txt bn_ab.txt bn_abcd.txt bn_8.txt bn_12.txt bn_bin.txt bn_long.txt; do
        enc_text=$(gnu basenc $e "$f" 2>/dev/null)
        bn_next; printf '%s' "$enc_text" > "$BN"
        compare_stdin "$BN" basenc $e -d
        compare_stdin "$BN" basenc $e -di
        bn_next; printf '%s!' "$enc_text" > "$BN"
        compare_stdin "$BN" basenc $e -d
        compare_stdin "$BN" basenc $e -di
        bn_next; printf '%s' "$enc_text" | head -c 7 > "$BN"
        compare_stdin "$BN" basenc $e -d
        bn_next; printf '%s' "$enc_text" | tr 'A-Za-z' 'a-zA-Z' > "$BN"
        compare_stdin "$BN" basenc $e -d
        compare_stdin "$BN" basenc $e -di
    done
done
for enc in 48656C6C6F 48656c6c6f 4865F 48GG '4865
6C' '48 65' 48zz65 '' 4 0 41 'AB
CD' 01000001 0100000 010000011 0102 '0100
0001' 0100000101000010 Hello HelloWorld Hell 'Hell~' HelloHell 'Hello
World' '00000' 'KKKKK' '%nSc0' '%nSc1' '%nSc00' 'aGVsbG8_' 'aGVsbG8/' 'aGVsbG8-' 'aGVsbG8+' 'aGVsbG8=' 'D1IMOR3F5GG7ERRI====' 'D1IMOR3F' 'nbswy3dp' 'NBSWY3DP' ; do
    bn_next; printf '%s' "$enc" > "$BN"
    for e in --base64 --base64url --base32 --base32hex --base16 --base2msbf --base2lsbf --z85; do
        compare_stdin "$BN" basenc $e -d
        compare_stdin "$BN" basenc $e -di
        compare_stdin "$BN" basenc $e --decode --ignore-garbage
    done
done
compare basenc
compare basenc a b
compare basenc --base64 a b
compare basenc --base64 -w x bn_a.txt
compare basenc --base64 -w -1 bn_a.txt
compare basenc --base64 --base32 bn_a.txt
compare basenc --base32 --base64 bn_a.txt
compare basenc --base64 nosuchfile_zz
compare basenc --base64 -d
compare basenc --base64 -- bn_a.txt
compare basenc --z85 bn_a.txt
compare basenc --z85 bn_abcd.txt
add_case status C - basenc --help
add_case status C - basenc --version
