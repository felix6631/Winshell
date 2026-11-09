# GNU comparison of csplit: each case splits a file in a fresh directory and compares the names and contents of the
# pieces, the sizes on standard output, the messages and the exit status. The file is "in" in that directory.
seq 1 20 > cs_num.txt
seq 1 12 | sed 's/^/L/' > cs_l.txt
printf 'a\nb\nSTART x\nc\nd\nSTART y\ne\nf\ng\nSTART z\nh\n' > cs_start.txt
printf 'one\ntwo\nthree no newline at the end' > cs_nonl.txt
printf 'x\r\ny\r\nz\r\n' > cs_crlf.txt
printf '' > cs_empty.txt
printf 'only\n' > cs_one.txt
cs_run() {
    local who=$1 input=$2 rc f
    shift 2
    rm -rf cs_dir
    mkdir cs_dir
    cp "$input" cs_dir/in
    ( cd cs_dir && MSYS2_ARG_CONV_EXCL="*" "$who" csplit "$@" > ../cs_out.txt 2> ../cs_err.txt ); rc=$?
    ( cd cs_dir && ls | sort | while IFS= read -r f; do [ "$f" = in ] && continue; echo "== $f"; od -c -- "$f"; done )
    echo '-- out'; cat cs_out.txt
    echo '-- err'; tr -d '\r' < cs_err.txt | sed -e 's#^[^ :]*csplit:#csplit:#' -e "s#^Try '[^ ]*csplit #Try 'csplit #"
    echo "-- rc $rc"
}
csplit_case() {
    local input=$1 g o
    shift
    total=$((total + 1))
    g=$(cs_run gnu "$input" "$@")
    o=$(cs_run ours "$input" "$@")
    if [ "$g" != "$o" ]; then
        bad=$((bad + 1))
        echo "DIFFERS: csplit $* < $input"
    fi
}

# line numbers
csplit_case cs_num.txt in 5
csplit_case cs_num.txt in 5 '{2}'
csplit_case cs_num.txt in 5 10 15
csplit_case cs_num.txt in 3 '{*}'
csplit_case cs_num.txt in 3 '{20}'
csplit_case cs_num.txt in 20
csplit_case cs_num.txt in 21
csplit_case cs_num.txt in 1
csplit_case cs_num.txt in 19 '{*}'
csplit_case cs_num.txt in 5 5
csplit_case cs_num.txt in 5 3
csplit_case cs_num.txt in 0
csplit_case cs_num.txt in 05
csplit_case cs_num.txt in +5
csplit_case cs_num.txt in ' 5'
csplit_case cs_num.txt in '5 '
csplit_case cs_num.txt in 0x5
csplit_case cs_num.txt in 5k
csplit_case cs_num.txt in 99999999999999999999
csplit_case cs_num.txt in 30
csplit_case cs_num.txt in 5 '{0}'
csplit_case cs_num.txt in 3 '{1}' 4
csplit_case cs_num.txt in 3 '{+3}'
csplit_case cs_num.txt in 3 '{ 3}'
csplit_case cs_num.txt in 5 '{-1}'
csplit_case cs_num.txt in 5 '{}'
csplit_case cs_num.txt in 5 '{x}'
csplit_case cs_num.txt in 5 '{3'
csplit_case cs_num.txt in 5 '{3}x'
csplit_case cs_num.txt in 5 '{*}x'
csplit_case cs_num.txt in 3 '{2}{2}'
csplit_case cs_num.txt in '{5}'
csplit_case cs_num.txt in '{*}'
csplit_case cs_num.txt in 5 '{1}' '{2}'
csplit_case cs_num.txt in ''
csplit_case cs_num.txt in 5 ''
csplit_case cs_num.txt in -- -1
csplit_case cs_num.txt in 1 '{18}'
csplit_case cs_num.txt in 1 '{19}'

# regular expressions and offsets
csplit_case cs_num.txt in /7/ %12%
csplit_case cs_num.txt in '/3/+1' '{*}'
csplit_case cs_num.txt in /zz/
csplit_case cs_num.txt in /zz/ '{*}'
csplit_case cs_num.txt in %zz%
csplit_case cs_num.txt in '/20/+1'
csplit_case cs_num.txt in '/19/+1'
csplit_case cs_num.txt in '/20/'
csplit_case cs_num.txt in '/20/+2'
csplit_case cs_num.txt in '/1/'
csplit_case cs_num.txt in '/1/-1'
csplit_case cs_num.txt in '/5/-2'
csplit_case cs_num.txt in '/5/-4'
csplit_case cs_num.txt in '/5/-5'
csplit_case cs_num.txt in '%5%'
csplit_case cs_num.txt in '%20%+1'
csplit_case cs_num.txt in '%1%-1'
csplit_case cs_num.txt in /5/ /5/
csplit_case cs_num.txt in /5/ '{1}'
csplit_case cs_num.txt in /1/ '{*}'
csplit_case cs_num.txt in /1/ '{3}'
csplit_case cs_num.txt in /1/ '{18}'
csplit_case cs_num.txt in /5/ /3/
csplit_case cs_num.txt in 5 /3/
csplit_case cs_num.txt in //
csplit_case cs_num.txt in %%
csplit_case cs_num.txt in '/3/' '{0}'
csplit_case cs_num.txt in '/3/ +3'
csplit_case cs_num.txt in '/3/+ 3'
csplit_case cs_num.txt in '/3/+-3'
csplit_case cs_num.txt in '/3/+3x'
csplit_case cs_num.txt in '/3/+'
csplit_case cs_num.txt in '/3/x'
csplit_case cs_num.txt in '/3/3'
csplit_case cs_num.txt in '/3/+0'
csplit_case cs_num.txt in '/3/-0'
csplit_case cs_num.txt in '/3/-3'
csplit_case cs_num.txt in '/3/+99999999999999999999'
csplit_case cs_num.txt in '/x'
csplit_case cs_num.txt in 'abc'
csplit_case cs_num.txt in '/5/' '/6/-2'
csplit_case cs_num.txt in '/5/+1' '/6/-2'
csplit_case cs_num.txt in '/5/' '/6/-1'
csplit_case cs_num.txt in '/5/+1' '/6/'
csplit_case cs_num.txt in '/5/+2' '/7/'
csplit_case cs_num.txt in '/5/+2' '/8/'
csplit_case cs_num.txt in '/5/-3' '/4/'
csplit_case cs_num.txt in '/5/-3' 6
csplit_case cs_num.txt in '/5/+3' 7
csplit_case cs_num.txt in '%5%+2' '/9/-1'
csplit_case cs_l.txt in '/L5$/' '/L9$/'
csplit_case cs_l.txt in '/L\(5\)$/'
csplit_case cs_l.txt in '/L5\|L7/' '{1}'
csplit_case cs_l.txt in '/L5|L7/' '{1}'
csplit_case cs_l.txt in '/L(5)$/'
csplit_case cs_l.txt in '/^L1[0-9]$/'
csplit_case cs_l.txt in '/L[3-4]$/' '{*}'
csplit_case cs_l.txt in '/L.*5/'
csplit_case cs_start.txt in '/START/' '{*}'
csplit_case cs_start.txt in '%START%' '/START/' '{*}'
csplit_case cs_start.txt in '/START/' '{1}'
csplit_case cs_start.txt in '/START/' '{3}'
csplit_case cs_start.txt in '/START/-1' '{*}'
csplit_case cs_start.txt in '/START/+1' '{*}'
csplit_case cs_start.txt in '/START/' '{*}' --suppress-matched
csplit_case cs_start.txt in '/START/+1' '{*}' --suppress-matched
csplit_case cs_start.txt in '/START/-1' '{*}' --suppress-matched
csplit_case cs_start.txt in '%START%' '{*}'

# --suppress-matched
csplit_case cs_l.txt --suppress-matched in '/L5$/'
csplit_case cs_l.txt --suppress-matched in '/L5$/+1'
csplit_case cs_l.txt --suppress-matched in '/L5$/-1'
csplit_case cs_l.txt --suppress-matched in '/L5$/+2'
csplit_case cs_l.txt --suppress-matched in '/L5$/' '/L9$/'
csplit_case cs_l.txt --suppress-matched in '/L5$/' 9
csplit_case cs_l.txt --suppress-matched in 5 '/L9$/'
csplit_case cs_l.txt --suppress-matched in 5 '{1}'
csplit_case cs_l.txt --suppress-matched in 5 '{2}'
csplit_case cs_l.txt --suppress-matched in '/L5$/' '{*}'
csplit_case cs_l.txt --suppress-matched in '%L5%'
csplit_case cs_l.txt --suppress-matched in '%L5%' '/L8$/'
csplit_case cs_l.txt --suppress-matched in '/L3$/' '/L3$/'
csplit_case cs_l.txt --suppress-matched in '/L/' '{*}'
csplit_case cs_l.txt --suppress-matched in '/L5$/+3' '/L7$/'
csplit_case cs_l.txt --suppress-matched in '/L5$/-3' '/L7$/'
csplit_case cs_l.txt --suppress-matched in 12
csplit_case cs_l.txt --suppress-matched in 11
csplit_case cs_l.txt --suppress-matched in 13

# options
csplit_case cs_num.txt -s in 5
csplit_case cs_num.txt -q in 5
csplit_case cs_num.txt --quiet in 5
csplit_case cs_num.txt --silent in 5 5
csplit_case cs_num.txt -z in 5 5
csplit_case cs_num.txt -z in '/1/' '{*}'
csplit_case cs_num.txt --elide-empty-files in '/20/+1'
csplit_case cs_num.txt -k in 30
csplit_case cs_num.txt --keep-files in /zz/
csplit_case cs_num.txt in 30
csplit_case cs_num.txt -n 3 in 5
csplit_case cs_num.txt -n 1 in 5 '{*}'
csplit_case cs_num.txt -n 0 in 5
csplit_case cs_num.txt -n x in 5
csplit_case cs_num.txt -n -1 in 5
csplit_case cs_num.txt -n 5 -b '%d' in 5
csplit_case cs_num.txt --digits=4 in 5
csplit_case cs_num.txt -f YY in 5
csplit_case cs_num.txt -f 'a-' -n 1 in 5 10
csplit_case cs_num.txt --prefix=pp in 5
csplit_case cs_num.txt -f '' in 5
csplit_case cs_num.txt -f sub/p in 5
csplit_case cs_num.txt -b '%03d.part' in 5
csplit_case cs_num.txt -b '%d' in 5
csplit_case cs_num.txt -b 'x%02x' in 5 7
csplit_case cs_num.txt -b '%X' in 5 7 9 11 13 15 17 19
csplit_case cs_num.txt -b '%c' in 5
csplit_case cs_num.txt -b 'a' in 5
csplit_case cs_num.txt -b '%s' in 5
csplit_case cs_num.txt -b '%5.2d' in 5
csplit_case cs_num.txt -b '%x%x' in 5
csplit_case cs_num.txt -b '%%' in 5
csplit_case cs_num.txt -b '%d%%' in 5
csplit_case cs_num.txt -b '%#o' in 5 6 7 8 9 10
csplit_case cs_num.txt -b '%#x' in 5 6 7 8 9 10 11 12 13 14 15 16 17
csplit_case cs_num.txt -b '%ld' in 5
csplit_case cs_num.txt -b '%#d' in 5
csplit_case cs_num.txt -b '%#i' in 5
csplit_case cs_num.txt -b '%#u' in 5
csplit_case cs_num.txt -b '%0d' in 5
csplit_case cs_num.txt -b '%05x' in 5
csplit_case cs_num.txt -b '%.d' in 5
csplit_case cs_num.txt -b '%.0d' in 5
csplit_case cs_num.txt -b '%00d' in 5
csplit_case cs_num.txt -b '%10' in 5
csplit_case cs_num.txt -b '%.' in 5
csplit_case cs_num.txt -b '%' in 5
csplit_case cs_num.txt -b '%x%' in 5
csplit_case cs_num.txt -b '%d%' in 5
csplit_case cs_num.txt -b '%%d' in 5
csplit_case cs_num.txt -b 'q%%%dq%%' in 5
csplit_case cs_num.txt -b '%+d' in 5
csplit_case cs_num.txt -b '% d' in 5
csplit_case cs_num.txt -b '%zd' in 5
csplit_case cs_num.txt -b '%e' in 5
csplit_case cs_num.txt -b '%*d' in 5
csplit_case cs_num.txt -b '%d%d' in 5
csplit_case cs_num.txt -b "%'d" in 5
csplit_case cs_num.txt -b '%i' in 5
csplit_case cs_num.txt --suffix-format='%d.txt' in 5

# other inputs and the operands
csplit_case cs_nonl.txt in 2
csplit_case cs_nonl.txt in 3
csplit_case cs_nonl.txt in '/three/'
csplit_case cs_nonl.txt in '/end$/'
csplit_case cs_crlf.txt in 2
csplit_case cs_crlf.txt in '/y/'
csplit_case cs_empty.txt in 1
csplit_case cs_empty.txt in '/a/'
csplit_case cs_empty.txt in '/a/' '{*}'
csplit_case cs_empty.txt in '%a%'
csplit_case cs_one.txt in 1
csplit_case cs_one.txt in 2
csplit_case cs_one.txt in '/only/'
csplit_case cs_one.txt in '/only/+1'
csplit_case cs_num.txt
csplit_case cs_num.txt in
csplit_case cs_num.txt nosuchfile 5
csplit_case cs_num.txt . 5
csplit_case cs_num.txt -- in 5
csplit_case cs_num.txt in 5 -- 10
add_case status C - csplit --bogus in 5
add_case status C - csplit --help
add_case status C - csplit --version
