# GNU comparison of logname: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
compare logname
compare logname --bogus
compare logname -x
compare logname extra
compare logname extra more
compare logname --
add_case status C - logname --help
add_case status C - logname --version
