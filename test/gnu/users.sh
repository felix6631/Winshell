# GNU comparison of users: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# The sessions are those of each side, so the errors and the exit codes are compared.
compare users --bogus
compare users -x
compare users one two
add_case status C - users
add_case status C - users --help
add_case status C - users --version
