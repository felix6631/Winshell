# GNU comparison of which: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# The paths differ (/usr/bin/sort and /c/Users/.../sort.exe): the exit status is what is compared.
for n in sort ls cat cmd notepad sh bash grep sed find nosuchprogram_xyz x.y ./nosuch "" ; do
    add_case status C - which "$n"
done
add_case status C - which sort ls nosuch_xyz
add_case status C - which nosuch_xyz sort
add_case status C - which -a sort
add_case status C - which -a nosuch_xyz
add_case status C - which
