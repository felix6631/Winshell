# GNU comparison of sum: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
printf 'binary\000\001\377\376data\n' > s_bin.txt
head -c 70000 /dev/zero > s_zero.txt
head -c 1024 /dev/zero > s_k1.txt
head -c 1025 /dev/zero > s_k1p.txt
head -c 512 /dev/zero > s_h1.txt
head -c 513 /dev/zero > s_h1p.txt
for f in $TEXTS s_bin.txt s_zero.txt s_k1.txt s_k1p.txt s_h1.txt s_h1p.txt; do
    for o in "" -r -s --sysv -rs -sr; do
        compare_words sum "$o" "$f"
        compare_words sum "$o $f"
    done
done
compare sum words.txt nonl.txt empty.txt
compare sum -s words.txt nonl.txt empty.txt
compare sum nosuchfile
compare sum words.txt nosuchfile nonl.txt
compare sum .
compare sum -
compare sum --bogus
compare sum -x
add_case status C - sum --help
add_case status C - sum --version
