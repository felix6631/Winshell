# GNU comparison of split: each case splits a file in a fresh directory and compares the names, sizes and contents of
# the pieces with the output and the exit status.
printf 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\n' > sp_lines.txt
printf 'no newline at the end' > sp_nonl.txt
printf '0123456789abcdefghijklmnopqrstuvwxyz' > sp_bytes.txt
printf 'a\n\nbb\n\n\nccc\ndddd\neeeee\nffffff\n' > sp_blank.txt
printf 'one two\nthree four five\nsix\nseven eight nine ten eleven\n12\n' > sp_words.txt
printf '' > sp_empty.txt
seq 1 2500 > sp_long.txt
printf 'a:b:c:d:e:f:g' > sp_colon.txt
split_case() {
    local input=$1 g o grc orc who
    shift
    total=$((total + 1))
    for who in gnu ours; do
        rm -rf sp_dir
        mkdir sp_dir
        cp "$input" sp_dir/in
        if [ $who = gnu ]; then
            ( cd sp_dir && gnu split "$@" > ../sp_out.txt 2> /dev/null ); grc=$?
            g=$( cd sp_dir && for f in $(ls | sort); do [ "$f" = in ] && continue; echo "== $f"; cat "$f"; done; cat ../sp_out.txt )
        else
            ( cd sp_dir && ours split "$@" > ../sp_out.txt 2> /dev/null ); orc=$?
            o=$( cd sp_dir && for f in $(ls | sort); do [ "$f" = in ] && continue; echo "== $f"; cat "$f"; done; cat ../sp_out.txt )
        fi
    done
    if [ "$g" != "$o" ] || [ $grc != $orc ]; then
        bad=$((bad + 1))
        echo "DIFFERS: split $* < $input (status $grc, ours $orc)"
    fi
}
for f in sp_lines.txt sp_nonl.txt sp_bytes.txt sp_blank.txt sp_words.txt sp_empty.txt; do
    split_case $f in
    split_case $f -l 1 in
    split_case $f -l 3 in
    split_case $f -3 in
    split_case $f --lines=4 in
    split_case $f -b 5 in
    split_case $f -b 1 in
    split_case $f -b 100 in
    split_case $f -b 1K in
    split_case $f -C 10 in
    split_case $f -C 1 in
    split_case $f -C 20 in
    split_case $f --line-bytes=7 in
    split_case $f -n 3 in
    split_case $f -n 1 in
    split_case $f -n 7 in
    split_case $f -n 100 in
    split_case $f -n 2/3 in
    split_case $f -n 3/3 in
    split_case $f -n l/3 in
    split_case $f -n l/5 in
    split_case $f -n l/1 in
    split_case $f -n l/2/3 in
    split_case $f -n r/3 in
    split_case $f -n r/2/3 in
    split_case $f -n 4 -e in
    split_case $f -n l/4 -e in
    split_case $f -d -l 3 in
    split_case $f -x -l 3 in
    split_case $f --numeric-suffixes=5 -l 3 in
    split_case $f -a 3 -l 3 in
    split_case $f -a 1 -b 5 in
    split_case $f -l 3 in pre_
    split_case $f -l 3 --additional-suffix=.txt in
    split_case $f -l 3 --verbose in
    split_case $f -b 5 -d -a 4 in out
    split_case $f -t : -l 2 in
    split_case $f -t : -C 4 in
done
split_case sp_colon.txt -t : -l 2 in
split_case sp_colon.txt -t : -C 4 in
split_case sp_colon.txt -t : -C 2 in
split_case sp_long.txt -l 1000 in
split_case sp_long.txt -b 1000 in
split_case sp_long.txt -d -l 5 in
split_case sp_long.txt -d -l 10 -a 2 in
split_case sp_long.txt -l 1 in
split_case sp_long.txt -n 1000 in
split_case sp_long.txt -n l/40 in
split_case sp_long.txt -n r/7 in
split_case sp_long.txt -x -l 25 in
split_case sp_lines.txt -l 0 in
split_case sp_lines.txt -b 0 in
split_case sp_lines.txt -n 0 in
split_case sp_lines.txt -n 4/3 in
split_case sp_lines.txt -n x in
split_case sp_lines.txt -a 0 in
split_case sp_lines.txt -a x in
split_case sp_lines.txt -b 5x in
split_case sp_lines.txt -l 3 -b 5 in
split_case sp_lines.txt -t ab -l 3 in
split_case sp_lines.txt -l 3 nosuchfile
split_case sp_lines.txt -l 3 in a b
split_case sp_lines.txt -a 1 -l 1 in
split_case sp_lines.txt --bogus in
split_case sp_lines.txt -l 3 --additional-suffix=a/b in
add_case status C - split --help
add_case status C - split --version
