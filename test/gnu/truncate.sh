# GNU comparison of truncate: each case makes a file of 100 bytes (or none), runs the tool on it, and compares the
# size that is left and the exit status.
# a size such as /30 must reach the tool as it is (MSYS would turn it into a path for a native program)
export MSYS2_ARG_CONV_EXCL='*'

trunc_case() {
    local start=$1 g o grc orc
    shift
    total=$((total + 1))
    rm -f t_file t_ref
    [ "$start" = none ] || head -c "$start" /dev/zero > t_file
    head -c 40 /dev/zero > t_ref
    gnu truncate "$@" > /dev/null 2>&1; grc=$?
    g=$(stat -c %s t_file 2>/dev/null || echo none)
    rm -f t_file
    [ "$start" = none ] || head -c "$start" /dev/zero > t_file
    ours truncate "$@" > /dev/null 2>&1; orc=$?
    o=$(stat -c %s t_file 2>/dev/null || echo none)
    if [ "$g" != "$o" ] || [ $grc != $orc ]; then
        bad=$((bad + 1))
        echo "DIFFERS: truncate (from $start) $* (size $g status $grc, ours $o status $orc)"
    fi
}
for start in 100 0 none 7; do
    for size in 0 1 10 50 100 150 1K 1KB 2M '+10' '-10' '+0' '-200' '<50' '<150' '>50' '>150' '/30' '%30' '/200' '%200' '/0' '%0' '+1K' '-1K'; do
        trunc_case "$start" -s "$size" t_file
        trunc_case "$start" --size="$size" t_file
        trunc_case "$start" -c -s "$size" t_file
    done
    trunc_case "$start" -r t_ref t_file
    trunc_case "$start" -r t_ref -s +5 t_file
    trunc_case "$start" -r t_ref -s -5 t_file
    trunc_case "$start" -r t_ref -s '<30' t_file
    trunc_case "$start" -r t_ref -s '>50' t_file
    trunc_case "$start" -r t_ref -s '%7' t_file
    trunc_case "$start" -r t_ref -s 5 t_file
    trunc_case "$start" -r nosuchfile t_file
    trunc_case "$start" -c -r t_ref t_file
done
trunc_case 100 -s 10 t_file t_other
trunc_case 100 -s 10 nodir/x t_file
trunc_case 100
trunc_case 100 t_file
trunc_case 100 -s
trunc_case 100 -s x t_file
trunc_case 100 -s 1x t_file
trunc_case 100 -s -- -5 t_file
trunc_case 100 -s 5K9 t_file
trunc_case 100 -s 99999999999999999999 t_file
trunc_case 100 -s 8E t_file
trunc_case 100 -s 10Z t_file
trunc_case 100 --bogus t_file
trunc_case 100 -x t_file
add_case status C - truncate --help
add_case status C - truncate --version
