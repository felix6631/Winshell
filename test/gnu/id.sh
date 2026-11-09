# GNU comparison of id: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# The numbers and the groups are those of the account on each side, so the errors and the exit codes are compared.
compare id --bogus
compare id -x
compare id -u -g
compare id -u -G
compare id -g -G
compare id -un -gn
compare id -n
compare id -r
compare id -z
compare id -zn
compare id -rn
compare id -nr
compare id -Z
compare id -ZG
compare id -u -Z
compare id -Z -u
compare id nosuchuser
compare id -u nosuchuser
compare id -G nosuchuser
compare id -n nosuchuser
compare id -un -gn nosuchuser
compare id -z nosuchuser
compare id --user --group
compare id --zero
compare id --name
compare id --real
compare id --context
add_case status C - id
add_case status C - id -a
add_case status C - id -u
add_case status C - id -ur
add_case status C - id -g
add_case status C - id -gr
add_case status C - id -G
add_case status C - id -Gr
add_case status C - id -zu
add_case status C - id -zg
add_case status C - id -zG
add_case status C - id --user
add_case status C - id --groups
add_case status C - id --help
add_case status C - id --version
add_case status C - id nosuchuser othernosuchuser
add_case status C - id -u nosuchuser
add_case status C - id -a -u -r
