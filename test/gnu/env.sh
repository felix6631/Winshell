# GNU comparison of env: the environment it builds for a command (printed by env itself, so both print the same) and its
# statuses (125 for env, 126 and 127 for the command). After -i no command is run: the other side does not find one
# without a PATH, which ours finds in its own directory.
compare env -i
compare env -i A=1
compare env -i A=1 B=2
compare env -i A=1 A=2
compare env -i A=
compare env -i 'A=b c'
compare env -i -u A
compare env --ignore-environment A=1
compare env -0 -i A=1 B=2
compare env --null -i A=1
compare env -0 -i
compare env A=1 printenv A
compare env -u LC_ALL printenv LC_ALL
compare env -- A=1 printenv A
compare env - A=1 env
compare env -i =x env
compare env nosuchcmd_zz
compare env -i nosuchcmd_zz
compare env A=1 nosuchcmd_zz
compare env -u
compare env -S
compare env --bogus
compare env -x
compare env -C nosuchdir_zz env
compare env -C
compare env false
compare env true
add_case status C - env --help
add_case status C - env --version
