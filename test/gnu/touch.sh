# GNU comparison of touch: the files that are made, the times that are set (only the ones that are given, not "now"), the
# messages and the statuses.
[ "$(type -t fs_case)" = function ] || . "$HERE/gnu/_fs.sh"
S='printf x > f; mkdir d; printf old > g; touch -d "2001-02-03 04:05:06" g f'
unset FS_FMT
fs_case touch "$S" new
fs_case touch "$S" new1 new2
fs_case touch "$S" f
fs_case touch "$S" -c nothere
fs_case touch "$S" -c f
fs_case touch "$S" d
fs_case touch "$S" -c d
fs_case touch "$S" d/new
fs_case touch "$S" nodir/new
fs_case touch "$S" f/new
fs_case touch "$S" -- -dash
fs_case touch "$S" 'a b'
fs_case touch "$S" ''
fs_case touch "$S"
fs_case touch "$S" -a new
fs_case touch "$S" -m new
fs_case touch "$S" --no-create nothere
fs_case touch "$S" -d bogus f
fs_case touch "$S" -t 99999999 f
fs_case touch "$S" -t abc f
fs_case touch "$S" -r nothere f
fs_case touch "$S" -r f -d '2000-01-01' f
fs_case touch "$S" --time=bogus f
fs_case touch "$S" -h f
fs_case touch "$S" - f
export FS_FMT='%s %Y'
fs_case touch "$S" -d '2020-01-02 03:04:05' f
fs_case touch "$S" -d '2020-01-02 03:04:05' new
fs_case touch "$S" -d '2020-01-02 03:04:05' f g
fs_case touch "$S" -d '@86400' f
fs_case touch "$S" -d 'yesterday 12:00' f
fs_case touch "$S" -d '2020-06-15' d
fs_case touch "$S" -t 202001020304 f
fs_case touch "$S" -t 202001020304.55 f
fs_case touch "$S" -t 0001020304 f
fs_case touch "$S" -r g f
fs_case touch "$S" -r g new
fs_case touch "$S" -r d f
fs_case touch "$S" -m -d '2010-10-10 10:10:10' f
fs_case touch "$S" -a -d '2010-10-10 10:10:10' f
fs_case touch "$S" -am -d '2010-10-10 10:10:10' f
fs_case touch "$S" -c -d '2010-10-10 10:10:10' nothere f
fs_case touch "$S" --date='2015-05-05 05:05:05' f
unset FS_FMT
fs_case touch "$S" 202001020304 f
add_case status C - touch --help
add_case status C - touch --version
