# GNU comparison of readlink: files that are not links, missing files and the canonical forms (their exit
# statuses; the canonical path is spelled /c/... here and /tmp/... in MSYS, so the text is not compared).
printf 'x' > r_file.txt
mkdir -p r_dir/sub
for target in r_file.txt r_dir r_dir/sub nosuchfile nodir/nosuchfile . .. ./r_dir/../r_file.txt '' r_file.txt/x; do
    compare readlink "$target"
    compare readlink -v "$target"
    compare readlink -n "$target"
    add_case status C - readlink -f "$target"
    add_case status C - readlink -e "$target"
    add_case status C - readlink -m "$target"
    add_case status C - readlink -fv "$target"
    add_case status C - readlink -z "$target"
done
compare readlink r_file.txt r_dir
compare readlink -n r_file.txt r_dir
add_case status C - readlink -f r_file.txt r_dir nosuchfile
add_case status C - readlink -e r_file.txt nosuchfile r_dir
compare readlink
compare readlink --bogus
compare readlink -x
add_case status C - readlink --help
add_case status C - readlink --version
