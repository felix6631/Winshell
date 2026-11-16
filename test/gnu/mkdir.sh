# GNU comparison of mkdir: the directories that are made, the messages and the statuses (the modes are not compared:
# Windows has none to set).
[ "$(type -t fs_case)" = function ] || . "$HERE/gnu/_fs.sh"
S='printf x > f; mkdir d; mkdir d/e'
fs_case mkdir "$S" a
fs_case mkdir "$S" a b c
fs_case mkdir "$S" d
fs_case mkdir "$S" -p d
fs_case mkdir "$S" -p d/e/f/g
fs_case mkdir "$S" -p x/y/z
fs_case mkdir "$S" x/y
fs_case mkdir "$S" f
fs_case mkdir "$S" -p f
fs_case mkdir "$S" f/x
fs_case mkdir "$S" -p f/x
fs_case mkdir "$S" -v a
fs_case mkdir "$S" -v -p x/y/z
fs_case mkdir "$S" -v d
fs_case mkdir "$S" -pv a/b a/c
fs_case mkdir "$S" -m 755 a
fs_case mkdir "$S" -m zzz a
fs_case mkdir "$S" --parents --verbose p/q
fs_case mkdir "$S" -- -dash
fs_case mkdir "$S" 'a b' "c'd"
fs_case mkdir "$S" a/
fs_case mkdir "$S" a//b
fs_case mkdir "$S" -p a//b/
fs_case mkdir "$S" .
fs_case mkdir "$S" -p .
fs_case mkdir "$S" ..
fs_case mkdir "$S" ''
fs_case mkdir "$S"
fs_case mkdir "$S" --bogus
fs_case mkdir "$S" -Z a
fs_case mkdir "$S" --context a
add_case status C - mkdir --help
add_case status C - mkdir --version
