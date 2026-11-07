# GNU comparison of nproc: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
compare nproc
compare nproc --all
compare nproc --ignore=1
compare nproc --ignore 1
compare nproc --ignore=0
compare nproc --ignore=1000
compare nproc --all --ignore=1
compare nproc --ignore=x
compare nproc --ignore=-1
compare nproc extra
compare nproc --bogus
add_case status C - nproc --help
add_case status C - nproc --version
