# GNU comparison of sync: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# The messages of the errors and the exit codes; what is written to the disk cannot be seen.
compare sync
compare sync .
compare sync sync_nosuchfile
compare sync . sync_nosuchfile
compare sync sync_nosuchfile .
compare sync -d
compare sync -d .
compare sync -d sync_nosuchfile
compare sync -f
compare sync -f .
compare sync -d -f
compare sync -d -f .
compare sync --data
compare sync --data .
compare sync --file-system .
compare sync --data --file-system .
compare sync --bogus
compare sync -x
compare sync -- .
add_case status C - sync --help
add_case status C - sync --version
