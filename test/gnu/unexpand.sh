# GNU comparison of unexpand: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
printf 'a       b       c\n        leading\n  \t  mixed   blanks\n\n   a   b      c        d\n       x\n      y z      w\n' > u_spec.txt
printf 'no newline    at the end    ' > u_nonl.txt
printf ' a\n  b\n   c\n    d\n     e\n      f\n       g\n        h\n         i\n          j\n' > u_steps.txt
printf 'x       y\n xx      y\n  xx     y\n   xx    y\n    xx   y\n     xx  y\n      xx y\n       xx\n' > u_runs.txt
printf 'a    b        c         d          e\n' > u_wide.txt
printf '\t\t  \t x\n       \ty\n \tz\n' > u_tabs.txt
printf '\b       x\nab\b      y\n' > u_back.txt
for f in words.txt nonl.txt empty.txt blank.txt tabs.txt u_spec.txt u_nonl.txt u_steps.txt u_runs.txt u_wide.txt u_tabs.txt u_back.txt big.txt; do
    compare_stdin "$f" unexpand
    compare_stdin "$f" unexpand -a
    compare_stdin "$f" unexpand --all
    compare_stdin "$f" unexpand --first-only
    compare_stdin "$f" unexpand -a --first-only
    for t in 1 2 3 4 5 8 '2,4' '4,8,12' '3,6,9' '2,+3' '4,/8' '+4' '/4' '5,10,/3' '1,2,3' '10,20' '8,16,24,32'; do
        compare_stdin "$f" unexpand -t "$t"
        compare_stdin "$f" unexpand --first-only -t "$t"
    done
done
compare unexpand u_spec.txt u_steps.txt
compare unexpand -a u_runs.txt u_wide.txt
compare unexpand -t 0 u_spec.txt
compare unexpand -t x u_spec.txt
compare unexpand -t 4,2 u_spec.txt
compare unexpand nosuchfile
compare unexpand -t
compare unexpand --bogus
compare unexpand -x
add_case status C - unexpand --help
add_case status C - unexpand --version
