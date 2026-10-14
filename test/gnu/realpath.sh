# GNU comparison of realpath: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# The absolute paths of MSYS and of this project differ (/tmp/... and /c/Users/...): the results are compared as
# paths relative to the current directory.
mkdir -p rp/dir/sub rp/other
printf 'x\n' > rp/file.txt
printf 'y\n' > rp/dir/inner.txt

for n in "." ".." "rp" "rp/" "rp//dir" "rp/./dir/../file.txt" "rp/dir/sub/../.." "rp/dir/sub/../../other" "rp/file.txt" "rp/nosuch" \
         "rp/nosuch/deeper" "rp/nosuch/.." "rp/file.txt/x" "rp/file.txt/.." "rp/dir/inner.txt/" "./rp" "rp/dir/../dir/sub" \
         "rp/DIR" "rp/Dir/Sub" "rp/a b" "rp/한글" "" "rp/./././dir" "rp/dir/." "rp/dir/.."; do
    compare realpath --relative-to=. -- "$n"
    compare realpath -e --relative-to=. -- "$n"
    compare realpath -m --relative-to=. -- "$n"
    compare realpath -s --relative-to=. -- "$n"
    compare realpath -q --relative-to=. -- "$n"
    compare realpath -z --relative-to=. -- "$n"
done
compare realpath --relative-to=. rp rp/dir rp/dir/sub rp/nosuch
compare realpath --relative-to=rp/dir rp/file.txt rp/dir/sub rp/other rp
compare realpath --relative-to=rp/dir rp/dir
compare realpath --relative-to=rp/dir/sub rp/other/x
compare realpath -m --relative-to=rp/nosuch rp/nosuch2/a
compare realpath --relative-to=rp/nosuch rp
compare realpath --relative-to=rp/dir --relative-base=rp rp/file.txt rp/dir/sub
compare realpath --relative-to=rp/dir --relative-base=rp rp/dir/sub rp/dir/sub/../../dir
compare realpath --relative-base=rp rp/dir/sub rp/dir
compare realpath --relative-base=. rp/dir
compare realpath
compare realpath --bogus rp
