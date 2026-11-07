# GNU comparison of tty: standard input is never a terminal here.
compare tty
compare tty -s
compare tty --silent
compare tty --quiet
compare tty extra
compare tty --bogus
compare tty -x
add_case status C - tty --help
add_case status C - tty --version
