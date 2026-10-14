# GNU comparison of test and [: sourced by gnu_compare.sh. The expressions are given as shell words.
chmod -R u+w tc 2> /dev/null; rm -rf tc
mkdir -p tc/dir
printf 'x\n' > tc/file.txt
: > tc/empty.txt
chmod u-w tc/file.txt 2> /dev/null

cmp_test() {
    add_case out C - test "$@"
    add_case out C - '[' "$@" ']'
}

# one to four arguments, by their number
for a in "" "x" "-n" "-z" "!" "(" ")" "-f" "-a" "-o" "=" "-e" "0" "-1"; do
    cmp_test "$a"
    cmp_test '!' "$a"
    cmp_test '!' '!' "$a"
done
cmp_test
for u in -n -z -e -f -d -s -r -w -x -L -h -b -c -p -S -u -g -k -O -G -t -q -N; do
    for v in "" x tc tc/file.txt tc/dir tc/empty.txt nosuch . words.txt; do
        cmp_test "$u" "$v"
        cmp_test '!' "$u" "$v"
    done
done

# < and > are in bash's test but not in the test of coreutils: not compared
for op in = == != -eq -ne -lt -le -gt -ge; do
    for a in "" a b abc 1 2 10 -1 " 5" "5 " 007 7 +3 3 99999999999999999999 -99999999999999999999 1.5 x; do
        for b in "" a b 1 2 10 -1 7 3 99999999999999999999 x; do
            cmp_test "$a" "$op" "$b"
        done
    done
done

for op in -nt -ot -ef; do
    for a in tc/file.txt tc/empty.txt tc/dir nosuch words.txt; do
        for b in tc/file.txt tc/empty.txt tc/dir nosuch .; do
            cmp_test "$a" "$op" "$b"
        done
    done
done

# expressions
cmp_test a -a b
cmp_test '' -a b
cmp_test a -o ''
cmp_test '' -o ''
cmp_test a -a '' -o b
cmp_test a -o b -a ''
cmp_test '!' a -o b
cmp_test '!' '(' a -o b ')'
cmp_test '(' a ')'
cmp_test '(' '' ')'
cmp_test '(' a -o b ')' -a c
cmp_test '(' '(' a ')' ')'
cmp_test '(' a
cmp_test a ')'
cmp_test a b
cmp_test a b c
cmp_test a b c d
cmp_test -n a b
cmp_test -f tc/file.txt -a -d tc/dir
cmp_test -f tc/file.txt -a -d tc/file.txt
cmp_test -f tc/file.txt -o -d tc/file.txt
cmp_test 1 -eq 1 -a 2 -lt 3
cmp_test 1 -eq 1 -a 2 -gt 3
cmp_test 1 -eq
cmp_test -eq 1 -eq 1
cmp_test x -eq 1
cmp_test 1 -eq x
cmp_test '!' 1 -eq 1
cmp_test '!' -f tc/file.txt
cmp_test '!' '!' -f tc/file.txt
cmp_test = = =
cmp_test '!' = '!'
cmp_test '-a' = '-a'
cmp_test '(' = ')'
cmp_test '(' -n x ')'
cmp_test '(' -n ')'
cmp_test -n -n
cmp_test -z -z
cmp_test -o -o -o
cmp_test -a -a -a
cmp_test '!' '!'
cmp_test '!' '!' '!'
cmp_test '!' -o '!'
cmp_test '!' '' -a '!' ''

# the closing bracket, and the options that are answered
add_case out C - '[' a
add_case out C - '[' a ']' x
add_case out C - '['
add_case out C - '[' ']'
add_case out C - '[' '!' ']'
add_case status C - test --version
add_case status C - test --help --help
add_case status C - '[' --version
