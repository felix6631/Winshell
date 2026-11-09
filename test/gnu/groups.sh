# GNU comparison of groups: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# The groups are those of the account on each side, so the errors and the exit codes are compared.
compare groups --bogus
compare groups -x
compare groups nosuchuser
compare groups nosuchuser othernosuchuser
add_case status C - groups --help
add_case status C - groups --version
add_case status C - groups nosuchuser
