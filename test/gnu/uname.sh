# GNU comparison of uname: the names of the systems differ (Windows_NT here), the machine and the node name do not.
compare uname -m
compare uname --machine
compare uname -n
compare uname --nodename
compare uname -p
compare uname -i
compare uname -mn
compare uname -nm
compare uname extra
compare uname --bogus
compare uname -x
add_case status C - uname
add_case status C - uname -a
add_case status C - uname -s -r -v
add_case status C - uname --help
add_case status C - uname --version
