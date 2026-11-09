# GNU comparison of hostid: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# The number is that of the host on each side, so the errors and the exit codes are compared.
compare hostid --bogus
compare hostid -x
compare hostid extra
compare hostid extra more
add_case status C - hostid
add_case status C - hostid --
add_case status C - hostid --help
add_case status C - hostid --version
