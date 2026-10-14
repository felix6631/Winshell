# GNU comparison of xargs: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
printf 'a b\nc d e\n\nf\n' > x1.txt
printf "a 'b c' \"d e\" f\\\\ g\n" > xquote.txt
printf 'one\0two words\0\0three\0' > x0.bin
printf 'a,b,c\nd,e\n' > xcomma.txt
printf 'a b \nc\nd e\n  f  \n' > xcont.txt
printf '' > xempty.txt
printf 'one two three four five six seven\n' > xlong.txt
printf "unmatched 'quote\n" > xbad.txt
printf 'x\ny\nSTOP\nz\n' > xeof.txt

for f in x1.txt words.txt xquote.txt xcont.txt xlong.txt xeof.txt; do
    for o in "" "-n 1" "-n 2" "-n 3" "-L 1" "-L 2" "-l" "-l2" "-I {}" "-I @ -n 5" "-s 30" "-s 12" "-t" "-r" "-E STOP" "-eSTOP" "-x -n 2" "-P 2" "-P 1 -n 1"; do
        set -- $o
        add_case out C $f xargs "$@" echo X
    done
done
add_case out C x1.txt xargs
add_case out C x1.txt xargs -n 2
add_case out C x1.txt xargs -I {} echo "[{}]" end
add_case out C x1.txt xargs -I[] echo "[]-[]"
add_case out C x1.txt xargs -i echo "<{}>"
add_case out C xempty.txt xargs echo none
add_case out C xempty.txt xargs -r echo none
add_case out C xempty.txt xargs -I {} echo none
add_case out C x0.bin xargs -0 -n 1 echo
add_case out C x0.bin xargs -0 echo
add_case out C x0.bin xargs -0 -I {} echo "<{}>"
add_case out C xcomma.txt xargs -d , echo
add_case out C xcomma.txt xargs -d , -n 1 echo
add_case out C xcomma.txt xargs -d '\n' echo
add_case out C xcomma.txt xargs -d '\n' -n 1 echo
add_case out C xcont.txt xargs -L 1 echo
add_case out C xcont.txt xargs -L 2 echo
add_case out C xcont.txt xargs -I {} echo "<{}>"
add_case out C xbad.txt xargs echo
add_case out C xquote.txt xargs -0 echo
add_case out C x1.txt xargs -a x1.txt echo
add_case out C x1.txt xargs --arg-file=xcont.txt -n 1 echo
add_case out C x1.txt xargs -a nosuch.txt echo
add_case out C x1.txt xargs nosuchprogram_xyz
add_case out C x1.txt xargs -n 0 echo
add_case out C x1.txt xargs -L 0 echo
add_case out C x1.txt xargs -s 1 echo
add_case out C x1.txt xargs -x -s 3 echo
add_case out C x1.txt xargs -n 1 false
add_case out C x1.txt xargs true
add_case out C x1.txt xargs -n 1 sh -c 'exit 255' x
add_case out C x1.txt xargs -n 1 sh -c 'test "$1" != c' x
add_case out C x1.txt xargs -n 2 printf '%s|'
add_case out C xlong.txt xargs --max-args=3 --max-chars=100 echo
add_case out C xlong.txt xargs --no-run-if-empty --verbose echo
