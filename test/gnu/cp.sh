# GNU comparison of cp: the files that are left (names, kinds, sizes, contents), the messages and the statuses. Symbolic
# links, modes and owners are not compared: those of MSYS are not those of Windows. (cp -r d d is not here: GNU makes d/d before it
# sees what it does, this one refuses first.)
[ "$(type -t fs_case)" = function ] || . "$HERE/gnu/_fs.sh"
S='printf hello > a; printf world > b; mkdir d; printf in > d/x; mkdir -p t/u; printf deep > t/u/v; printf same > s'
unset FS_FMT
fs_case cp "$S" a new
fs_case cp "$S" a b
fs_case cp "$S" a d
fs_case cp "$S" a d/
fs_case cp "$S" a b d
fs_case cp "$S" a b new
fs_case cp "$S" a b nothere/x
fs_case cp "$S" nothere new
fs_case cp "$S" a a
fs_case cp "$S" a ./a
fs_case cp "$S" d new
fs_case cp "$S" d d2
fs_case cp "$S" -r d d2
fs_case cp "$S" -r d t
fs_case cp "$S" -r d t/
fs_case cp "$S" -r t d
fs_case cp "$S" -r t u2
fs_case cp "$S" -R t u2
fs_case cp "$S" -r d/ new
fs_case cp "$S" -r d/. new
fs_case cp "$S" -r d/. t
fs_case cp "$S" -rv t new
fs_case cp "$S" -v a new
fs_case cp "$S" -v a b
fs_case cp "$S" -v a b d
fs_case cp "$S" -n a b
fs_case cp "$S" -n a new
fs_case cp "$S" -nv a b
fs_case cp "$S" -f a b
fs_case cp "$S" -u a b
fs_case cp "$S" -T a new
fs_case cp "$S" -T a d
fs_case cp "$S" -T a b
fs_case cp "$S" -t d a b
fs_case cp "$S" -t nodir a
fs_case cp "$S" -t a b
fs_case cp "$S" -b a b
fs_case cp "$S" -bv a b
fs_case cp "$S" --backup=numbered a b
fs_case cp "$S" --backup=simple -S .orig a b
fs_case cp "$S" -p a new
fs_case cp "$S" -a a new
fs_case cp "$S" -ar t new
fs_case cp "$S" --parents t/u/v d
fs_case cp "$S" -d a new
fs_case cp "$S" -l a new
fs_case cp "$S" a
fs_case cp "$S"
fs_case cp "$S" -- a new
fs_case cp "$S" 'a' 'sp ace'
fs_case cp "$S" a ''
fs_case cp "$S" '' new
fs_case cp "$S" a/x new
fs_case cp "$S" a b/x
add_case status C - cp --help
add_case status C - cp --version
