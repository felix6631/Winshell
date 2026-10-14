# GNU comparison of basename: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# a name of two slashes is a root of its own for Cygwin (//server); here it is "/", as on Linux
for n in "/usr/bin/sort" "sort" "/" "///" "a/" "a//" "/a" "//a//" "a/b/c" "a/b/c/" "" "." ".." "./a" "a." ".hidden" "a b/c d" "/a/b.txt" "dir/.txt" "x.h.h" "가/나다.txt"; do
    compare basename -- "$n"
    compare basename -a -- "$n"
    compare basename -z -- "$n"
    for suffix in ".txt" ".h" "txt" "a" "sort" "" "/" ".h.h" "나다.txt"; do
        compare basename -- "$n" "$suffix"
        compare basename -s "$suffix" -- "$n"
    done
done
compare basename -a /a/b.c /d/e.c x
compare basename -s .c /a/b.c /d/e.c x.c
compare basename -z -a a/b c/d
compare basename -as .c a/b.c
compare basename --suffix=.c --multiple a/b.c
compare basename
compare basename a b c
compare basename -a
compare basename -s
compare basename --bogus a
