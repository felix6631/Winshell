# GNU comparison of rmdir: what is left, the messages and the statuses.
[ "$(type -t fs_case)" = function ] || . "$HERE/gnu/_fs.sh"
S='mkdir -p a/b/c e f; printf x > f/file; printf y > a/top'
fs_case rmdir "$S" e
fs_case rmdir "$S" e f
fs_case rmdir "$S" f
fs_case rmdir "$S" a
fs_case rmdir "$S" a/b/c
fs_case rmdir "$S" -p a/b/c
fs_case rmdir "$S" -pv a/b/c
fs_case rmdir "$S" -v e
fs_case rmdir "$S" nosuch
fs_case rmdir "$S" e nosuch
fs_case rmdir "$S" -p nosuch/x
fs_case rmdir "$S" --ignore-fail-on-non-empty f
fs_case rmdir "$S" --ignore-fail-on-non-empty a
fs_case rmdir "$S" f/file
fs_case rmdir "$S" e/
fs_case rmdir "$S" e//
fs_case rmdir "$S" a/b/c/
fs_case rmdir "$S" -p a/b/c/
fs_case rmdir "$S" .
fs_case rmdir "$S" ..
fs_case rmdir "$S" -p e/
fs_case rmdir "$S"
fs_case rmdir "$S" --bogus
fs_case rmdir "$S" -- e
add_case status C - rmdir --help
add_case status C - rmdir --version
