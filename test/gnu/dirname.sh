# GNU comparison of dirname: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for n in "/usr/bin/sort" "sort" "/" "///" "a/" "a//" "/a" "///a//" "a/b/c" "a/b/c/" "a//b" "a///b//" "" "." ".." "./a" "a/." "a/.." "/a/b" "a b/c d" "가/나다.txt" "/가"; do
    compare dirname -- "$n"
    compare dirname -z -- "$n"
done
compare dirname /a/b c/d e
compare dirname -z /a/b c/d
compare dirname
compare dirname --bogus a
