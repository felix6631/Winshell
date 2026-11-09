# GNU comparison of nice: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# The niceness of the process is not the same number on Windows, so only what a command prints and the exit codes are compared.
compare nice echo hi
compare nice -n 5 echo hi
compare nice -n 0 echo hi
compare nice -n -5 echo hi
compare nice -n +5 echo hi
compare nice -n 100 echo hi
compare nice -n -100 echo hi
compare nice -n 5 -- echo hi
compare nice -- echo a b
compare nice -5 echo hi
compare nice --5 echo hi
compare nice -+5 echo hi
compare nice -3 -n 4 echo hi
compare nice --adjustment=3 echo hi
compare nice --adjustment 3 echo hi
compare nice -n5 echo hi
compare nice -n x echo hi
compare nice -n 1x echo hi
compare nice -n '' echo hi
compare nice -n 5
compare nice -5
compare nice --adjustment=3
compare nice nosuchcommand_zz
compare nice -n 5 nosuchcommand_zz
compare nice echo -n x
compare nice -n 5 echo -n x
add_case status C - nice
add_case status C - nice --bogus
add_case status C - nice -x
add_case status C - nice -n
add_case status C - nice --help
add_case status C - nice --version
