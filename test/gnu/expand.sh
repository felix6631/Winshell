# GNU comparison of expand: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
printf 'a\tb\tc\n\tleading\ttab\n  \tmixed\t blanks\n\t\ttwo\n\bback\tspace\nx\r\ty\n\n   spaces only\nlong text goes here\tx\tyy\tzzz\t\n' > x_spec.txt
printf 'no newline\tat the end' > x_nonl.txt
printf '\ta\n \tb\n  \tc\n   \td\n    \te\n     \tf\n      \tg\n       \th\n        \ti\n' > x_steps.txt
printf '\xc3\xa9\tx\n\xea\xb0\x80\xeb\x82\x98\tx\n' > x_utf8.txt
for f in tabs.txt words.txt nonl.txt empty.txt blank.txt x_spec.txt x_nonl.txt x_steps.txt big.txt; do
    compare_stdin "$f" expand
    compare_stdin "$f" expand -i
    compare_stdin "$f" expand --initial
    for t in 1 2 3 4 5 8 16 '2,4' '4,8,12' '3,6,9,12' '2,+3' '4,/8' '+4' '/4' '5,10,/3' '1,2,3' '10,20' '4 8 12' '8,16,24,32'; do
        compare_stdin "$f" expand -t "$t"
        compare_stdin "$f" expand -i -t "$t"
    done
done
compare_stdin x_steps.txt expand -4
compare_stdin x_steps.txt expand -t4 -t8
compare_stdin x_steps.txt expand --tabs=3
compare_stdin x_steps.txt expand --tabs 5 -i
compare expand x_spec.txt x_steps.txt
compare expand x_steps.txt x_nonl.txt
compare expand -t 0 x_spec.txt
compare expand -t x x_spec.txt
compare expand -t 4,2 x_spec.txt
compare expand -t 4,4 x_spec.txt
compare expand -t 2,/4,6 x_spec.txt
compare expand -t / x_spec.txt
compare expand -t 99999999999999999999 x_spec.txt
compare expand nosuchfile
compare expand -t
compare expand --bogus
compare expand -x
add_case status C - expand --help
add_case status C - expand --version
