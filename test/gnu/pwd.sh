# GNU comparison of pwd: the options and the statuses (the directory is written another way on each side).
add_case status C - pwd
add_case status C - pwd -L
add_case status C - pwd -P
add_case status C - pwd -LP
add_case status C - pwd -PL
add_case status C - pwd --logical
add_case status C - pwd --physical
add_case status C - pwd --help
add_case status C - pwd --version
compare pwd --bogus
compare pwd -x
add_case status C - pwd extra
add_case status C - pwd -L extra
add_case status C - pwd -- extra
