# GNU comparison of kill (the kill of MSYS is the one of util-linux: the whole list of signals and the messages for no operand
# are its own): the names and numbers it translates, and the errors; no process is signalled
# (a process that does not exist is the one asked for).
compare kill -l 9
compare kill -l TERM
compare kill -l SIGTERM
compare kill -l 256
compare kill -l bogus
compare kill -l 999
compare kill -s FOO 999999
compare kill -s
compare kill -n
compare kill -n 9999 999999
compare kill -FOO 999999
compare kill abc
compare kill -s TERM abc
compare kill -s 0 999999
compare kill -0 999999
compare kill -TERM 999999
compare kill -9 999999
compare kill -s KILL 999999 999998
compare kill -s INT 999999
compare kill -- -9 999999
compare kill --bogus
add_case status C - kill --help
add_case status C - kill --version
