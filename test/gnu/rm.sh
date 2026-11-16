# GNU comparison of rm: what is left, the messages and the statuses. (Every case runs in a directory of its own, made for it.)
[ "$(type -t fs_case)" = function ] || . "$HERE/gnu/_fs.sh"
S='printf hello > a; printf world > b; mkdir d; printf in > d/x; mkdir -p t/u; printf deep > t/u/v; mkdir e; printf z > "sp ace"; printf q > -dash'
unset FS_FMT
fs_case rm "$S" a
fs_case rm "$S" a b
fs_case rm "$S" nothere
fs_case rm "$S" a nothere b
fs_case rm "$S" -f nothere
fs_case rm "$S" -f a nothere
fs_case rm "$S" d
fs_case rm "$S" e
fs_case rm "$S" -d e
fs_case rm "$S" -d d
fs_case rm "$S" -r d
fs_case rm "$S" -r e
fs_case rm "$S" -rf t
fs_case rm "$S" -R t
fs_case rm "$S" -r d t e
fs_case rm "$S" -rf nothere
fs_case rm "$S" -rv t
fs_case rm "$S" -v a b
fs_case rm "$S" -v d
fs_case rm "$S" -rv d
fs_case rm "$S" -fv nothere
fs_case rm "$S" -- -dash
fs_case rm "$S" ./-dash
fs_case rm "$S" "sp ace"
fs_case rm "$S" -r d/
fs_case rm "$S" d/x
fs_case rm "$S" d/x d
fs_case rm "$S" t/u/v
fs_case rm "$S" a/
fs_case rm "$S" a/x
fs_case rm "$S" -f a/x
fs_case rm "$S" -r .
fs_case rm "$S" -r ..
fs_case rm "$S" -r ./
fs_case rm "$S" .
fs_case rm "$S" ..
fs_case rm "$S" -rf d/.
fs_case rm "$S" -r --no-preserve-root d
fs_case rm "$S" --one-file-system -r d
fs_case rm "$S" -I a
fs_case rm "$S" --interactive=never a
fs_case rm "$S" ''
fs_case rm "$S" -f ''
fs_case rm "$S"
fs_case rm "$S" -f
fs_case rm "$S" -x a
add_case status C - rm --help
add_case status C - rm --version
