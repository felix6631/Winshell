# The comparison of md5sum, sha1sum, sha224sum, sha256sum, sha384sum and sha512sum: they differ in the algorithm only.
# Every sha*sum.sh sources this file and calls sum_cases with the name of its tool. (As a section of its own this file
# only defines the function.)
sum_cases() {
    local tool=$1 f
    printf 'binary\000\001\377\376data\n' > s_bin.txt
    printf 'x' > s_x.txt
    printf 'name with space' > "s_sp ace.txt"
    for f in empty.txt words.txt nonl.txt tabs.txt blank.txt big.txt s_bin.txt s_x.txt; do
        compare_stdin "$f" "$tool"
        compare "$tool" "$f"
        compare "$tool" -b "$f"
        compare "$tool" -t "$f"
        compare "$tool" --tag "$f"
        compare "$tool" -z "$f"
    done
    # the lengths around the blocks of 64 and 128 bytes
    for n in 1 55 56 57 63 64 65 111 112 113 119 120 127 128 129 255 256 257 1000 65535 65536 65537; do
        head -c "$n" /dev/zero | tr "\000" x > "s_len$n.txt"
        compare "$tool" "s_len$n.txt"
    done
    compare "$tool" words.txt nonl.txt empty.txt
    compare "$tool" --tag words.txt nonl.txt
    compare "$tool" -z words.txt nonl.txt
    compare "$tool" "s_sp ace.txt"
    compare "$tool" nosuchfile
    compare "$tool" words.txt nosuchfile nonl.txt
    compare "$tool" .
    compare "$tool" -
    compare "$tool" --tag -t words.txt
    compare "$tool" -t -b words.txt
    compare "$tool" -b -t words.txt

    # checking: sums made by the GNU tool of the same name
    gnu "$tool" words.txt nonl.txt empty.txt tabs.txt > s_good.txt
    gnu "$tool" --tag words.txt nonl.txt > s_tag.txt
    gnu "$tool" -t words.txt nonl.txt > s_text.txt
    gnu "$tool" words.txt nosuchfile nonl.txt > s_missing.txt 2>/dev/null
    sed '2s/^./0/' s_good.txt > s_bad.txt
    sed '2s/^\(.\)/\U\1/' s_good.txt > s_upper.txt
    tr 'a-f' 'A-F' < s_good.txt | sed 's/ \*[A-Z.]*/ */' > /dev/null
    { cat s_good.txt; echo 'this line is improperly formatted'; echo; echo '# a comment'; cat s_tag.txt; } > s_mixed.txt
    printf 'not a checksum line\nanother one\n' > s_none.txt
    : > s_empty.txt
    gnu "$tool" "s_sp ace.txt" > s_escaped.txt
    for c in s_good.txt s_tag.txt s_text.txt s_missing.txt s_bad.txt s_upper.txt s_mixed.txt s_none.txt s_empty.txt s_escaped.txt; do
        compare "$tool" -c "$c"
        compare "$tool" --check "$c"
        compare "$tool" -c --quiet "$c"
        compare "$tool" -c --status "$c"
        compare "$tool" -c --warn "$c"
        compare "$tool" -c -w --strict "$c"
        compare "$tool" -c --strict "$c"
        compare "$tool" -c --ignore-missing "$c"
        compare_stdin "$c" "$tool" -c
        compare_stdin "$c" "$tool" -c -
    done
    compare "$tool" -c s_good.txt s_bad.txt
    compare "$tool" -c s_good.txt nosuchfile
    compare "$tool" -c nosuchfile
    compare "$tool" -c words.txt

    # options that do not go together, and the usual errors
    compare "$tool" -c --tag s_good.txt
    compare "$tool" --status words.txt
    compare "$tool" --quiet words.txt
    compare "$tool" --warn words.txt
    compare "$tool" --strict words.txt
    compare "$tool" --ignore-missing words.txt
    compare "$tool" -c -z s_good.txt
    compare "$tool" --bogus
    compare "$tool" -x
    add_case status C - "$tool" --help
    add_case status C - "$tool" --version
}
