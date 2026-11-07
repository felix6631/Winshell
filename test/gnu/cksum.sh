# GNU comparison of cksum: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
printf 'x' > k_x.txt
printf 'binary\000\001\377\376data\n' > k_bin.txt
head -c 70000 /dev/zero > k_zero.txt
for f in empty.txt words.txt nonl.txt tabs.txt blank.txt big.txt k_x.txt k_bin.txt k_zero.txt lines.txt; do
    compare_stdin "$f" cksum
    compare cksum "$f"
done
compare cksum words.txt nonl.txt empty.txt
compare cksum nosuchfile
compare cksum words.txt nosuchfile nonl.txt
compare cksum .
compare cksum -
compare cksum --bogus
compare cksum -x
add_case status C - cksum --help
add_case status C - cksum --version
