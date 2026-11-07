# GNU comparison of hostname: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
compare hostname
# (the MSYS hostname has no -s: the options of ours are in hostname_test)
add_case status C - hostname --help
add_case status C - hostname --bogus
add_case status C - hostname a b
