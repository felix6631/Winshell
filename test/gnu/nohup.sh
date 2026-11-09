# GNU comparison of nohup: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# Standard output is not a terminal here, so the command's output goes through as it is.
compare nohup echo hi
compare nohup echo a b c
compare nohup -- echo hi
compare nohup echo -n x
compare nohup nosuchcommand_zz
compare nohup nosuchcommand_zz arg
compare nohup
add_case status C - nohup
add_case status C - nohup --bogus
add_case status C - nohup --help
add_case status C - nohup --version
compare_stdin nl.txt nohup cat
