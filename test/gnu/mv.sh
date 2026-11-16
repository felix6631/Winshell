# GNU comparison of mv: what is left, the messages and the statuses (no links, no modes, no owners).
[ "$(type -t fs_case)" = function ] || . "$HERE/gnu/_fs.sh"
S='printf hello > a; printf world > b; mkdir d; printf in > d/x; mkdir -p t/u; printf deep > t/u/v; printf same > s; mkdir e'
unset FS_FMT
fs_case mv "$S" a new
fs_case mv "$S" a b
fs_case mv "$S" a d
fs_case mv "$S" a d/
fs_case mv "$S" a b d
fs_case mv "$S" a b e
fs_case mv "$S" a b new
fs_case mv "$S" a b nothere/x
fs_case mv "$S" nothere new
fs_case mv "$S" a a
fs_case mv "$S" a ./a
fs_case mv "$S" d new
fs_case mv "$S" d e
fs_case mv "$S" d t
fs_case mv "$S" d a
fs_case mv "$S" a d2/
fs_case mv "$S" t t/u
fs_case mv "$S" t t/u/inner
fs_case mv "$S" t/u/v .
fs_case mv "$S" -v a new
fs_case mv "$S" -v a b
fs_case mv "$S" -v a b d
fs_case mv "$S" -v d new
fs_case mv "$S" -n a b
fs_case mv "$S" -n a new
fs_case mv "$S" -nv a b
fs_case mv "$S" -f a b
fs_case mv "$S" -u a b
fs_case mv "$S" -T a new
fs_case mv "$S" -T a d
fs_case mv "$S" -T a b
fs_case mv "$S" -T d e
fs_case mv "$S" -T a b s
fs_case mv "$S" -t d a b
fs_case mv "$S" -t nodir a
fs_case mv "$S" -t a b
fs_case mv "$S" -b a b
fs_case mv "$S" -bv a b
fs_case mv "$S" --backup=numbered a b
fs_case mv "$S" --backup=simple -S .orig a b
fs_case mv "$S" -S .x -b a b
fs_case mv "$S" a
fs_case mv "$S"
fs_case mv "$S" -- a new
fs_case mv "$S" 'a' 'sp ace'
fs_case mv "$S" a ''
fs_case mv "$S" '' new
fs_case mv "$S" a/x new
fs_case mv "$S" a b/x
fs_case mv "$S" --strip-trailing-slashes d/ new
fs_case mv "$S" -x a new
add_case status C - mv --help
add_case status C - mv --version
