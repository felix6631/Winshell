# GNU comparison of grep: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
printf 'abc\nabcabc\nxyz\nfoo.bar\nfoo+bar\n(paren)\n[brk]\nA1b2\n a b \n\nfoo  bar\nBAR baz\n' > re.txt
printf '가나다\nabc 한글 def\nÉcole\nécole\nñandú\n한\nx\n' > utf.txt
mkdir -p tree/sub tree/.hid
printf 'needle here\nplain\n' > tree/a.txt
printf 'another needle\n' > tree/sub/b.c
printf 'hidden needle\n' > tree/.hid/c.txt
printf 'bin\0needle\n' > tree/bin.dat

# The whole matrix for two files with varied lines; the files with few lines (a single line without a
# line feed, only empty lines, repeated lines) get a few patterns that behave differently there.
GREP_FILES="words.txt re.txt"
GREP_SMALL_FILES="dup.txt nonl.txt blank.txt"
BRE_PATTERNS=("o" "foo" "^f" "r$" "." "a.c" "x*" "[a-c]" "[^a-c]" "[[:digit:]]\+" "\(ab\)*c" "a\{2\}" "ab\|ba" \
              "\bfoo" "foo\>" "\w\+" "\W" "\s" "o\{1,\}" "^$" "^" "$" "*a" "a\{0\}" "[]a]" "[a-]" "\(a\)\1" \
              "\<a" "o\?" "A" "bar" "^.\{3\}$" "[[:upper:]]" "b\+" "^\(.\)\(.\).\2\1" "fo*" "\.")
ERE_PATTERNS=("a|b" "(ab)+" "a{2,3}" "x?y" "^(abc)+$" "o+" "(foo|bar)+" "^$|x" "a|" "(a)(b)\2" "[0-9]+" \
              "\(paren\)" "^[^ ]+ [^ ]+$" "b{0,1}c" "(^| )foo" ".+" "a**" "()" "(a|b)*c" "foo\.bar" "a{,2}b")
GREP_OPTIONS=("" "-i" "-v" "-c" "-n" "-o" "-w" "-x" "-on" "-b" "-nb" "-vc" "-ci" "-ow" "-m1" "-A1" "-B1" "-C1" "-H" "-q")
SMALL_BRE=("o" "^$" "^" "$" "x*" "a" "\(a\)\1")
SMALL_ERE=("a|b" "^$|x" "a*" "(a|b)+")

for f in $GREP_FILES; do
    for pat in "${BRE_PATTERNS[@]}"; do
        for o in "" -i -v -c -n -o -w -x -m1 -C1 -ow; do
            compare_grep $o -e "$pat" $f
        done
    done
    for pat in "${ERE_PATTERNS[@]}"; do
        for o in "" -i -v -c -o -w -x -n; do
            compare_grep -E $o -e "$pat" $f
        done
    done
    for o in "${GREP_OPTIONS[@]}"; do
        compare_grep $o foo $f
        compare_grep -F $o 'o' $f
    done
done

for f in $GREP_SMALL_FILES; do
    for pat in "${SMALL_BRE[@]}"; do
        for o in "" -i -v -c -n -o -w -x -m1 -C1; do
            compare_grep $o -e "$pat" $f
        done
    done
    for pat in "${SMALL_ERE[@]}"; do
        for o in "" -v -c -o -n; do
            compare_grep -E $o -e "$pat" $f
        done
    done
done

# several files, several patterns, and what a name does to the output
compare_grep foo words.txt re.txt dup.txt
compare_grep -c o words.txt re.txt nosuch.txt
compare_grep -l o words.txt re.txt dup.txt empty.txt
compare_grep -L o words.txt re.txt dup.txt empty.txt
compare_grep -n -H foo re.txt
compare_grep -h foo words.txt re.txt
compare_grep -e foo -e abc -e xyz re.txt
compare_grep -E -e 'a|b' -e 'c' re.txt
compare_grep -F -e foo -e 'o.b' re.txt
compare_grep -F -x -e abc -e xyz re.txt
compare_grep -F -w -e foo -e bar re.txt
compare_grep -f re.txt words.txt
compare_grep -f empty.txt words.txt
compare_grep -v -f empty.txt words.txt
compare_grep -A2 -B2 -n foo re.txt words.txt
compare_grep -C1 -n o dup.txt big.txt
compare_grep -m2 -A1 o words.txt
compare_grep -m0 o words.txt
compare_grep -c -m2 o words.txt
compare_grep -on -b o re.txt
compare_grep -ob -i 'AB' re.txt
compare_grep -2 foo re.txt
compare_grep -A 0 foo re.txt
compare_grep -ni -w -e foo -e baz re.txt
compare_grep -x '' blank.txt
compare_grep -c '' empty.txt
compare_grep -c '^' nonl.txt
compare_grep -w '' re.txt
compare_grep -o '' re.txt
compare_grep -E 'a{1,2}{2}' re.txt
compare_grep -E '{1}' re.txt
compare_grep -E -c '{1}x' re.txt
compare_grep -- -x re.txt
compare_grep -e '' re.txt
compare_grep -z foo re.txt
compare_grep -zc '' re.txt
compare_grep -Z -l foo words.txt re.txt
compare_grep -Z -c foo words.txt re.txt
compare_grep -a foo tree/bin.dat
compare_grep foo tree/bin.dat
compare_grep -c needle tree/bin.dat
compare_grep -I needle tree/bin.dat
compare_grep -r needle tree
compare_grep -rn needle tree/sub
compare_grep -rl needle tree
compare_grep -rc needle tree
compare_grep -rh needle tree
compare_grep -r --include='*.c' needle tree
compare_grep -r --exclude='*.txt' needle tree
compare_grep -r --exclude-dir=sub needle tree
compare_grep needle tree/a.txt tree/sub
compare_grep -s needle tree/sub
compare_grep -d skip needle tree/a.txt tree/sub

# UTF-8: a character is one character
for pat in "." "^.$" "^..$" "[가-힣]" "[^a-z]" "é" "É" "^.\{3\}$" "한" "c 한" "\w\+" "[[:alpha:]]\+"; do
    for o in "" -i -o -c -v -n; do
        compare_grep $o -e "$pat" utf.txt
    done
done
compare_grep -E '^.{2}$' utf.txt
compare_grep -io 'école' utf.txt
compare_grep -F -i 'ÉCOLE' utf.txt

# errors: the status is what counts
for bad_pattern in '\(' '\)' 'a\{' 'a\{1' 'a\{2,1\}' '[' '[a' '[[:foo:]]' '[:alpha:]' '[b-a]' '\1' 'a\' '\(a\)\2'; do
    compare_grep -e "$bad_pattern" re.txt
done
for bad_pattern in '(' ')' 'a{1' 'a{2,1}' '[' '(a' 'a)' '\1' 'a\' '+' '*a' '?' '(*a)' '(+)'; do
    compare_grep -E -e "$bad_pattern" re.txt
done
