# GNU comparison of whoami: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
compare whoami
compare whoami extra
compare whoami --bogus
compare whoami -x
add_case status C - whoami --help
add_case status C - whoami --version
