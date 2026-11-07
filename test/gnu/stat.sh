# GNU comparison of stat: the fields that mean the same on Windows (size, name, type, times, links, inode, device).
# The blocks, the owner and the mount point are Windows' own and are checked by stat_test.
printf 'hello' > stat_a.txt
: > stat_empty.txt
mkdir -p stat_dir
for f in stat_a.txt stat_empty.txt stat_dir words.txt big.txt .; do
    for fmt in '%s' '%n' '%F' '%Y' '%y' '%W' '%w' '%h' '%i' '%d' '%D' '%t %T' '%N' '%a %A %f' '%U' '[%10s][%-10s][%010s]' '[%5n]' '%s %n %F' 'x%%y' '%Z' '%z' '%q' '%' 'plain'; do
        compare stat -c "$fmt" "$f"
    done
    compare stat --format='%n:%s' "$f"
    compare stat --printf='%n\t%s\n' "$f"
    compare stat --printf='%n %s' "$f"
    compare stat --printf='\x41\101\e[0m\a\b\f\r\v\\\"\q' "$f"
    add_case status C - stat "$f"
    add_case status C - stat -t "$f"
    add_case status C - stat -L "$f"
done
compare stat -c '%n %s' stat_a.txt stat_empty.txt
compare stat -c '%n %s' stat_a.txt nosuchfile stat_empty.txt
compare stat -c '%n' nosuchfile
compare stat nosuchfile
compare stat
compare stat --bogus
compare stat -x
compare stat -c
add_case status C - stat --help
add_case status C - stat --version
