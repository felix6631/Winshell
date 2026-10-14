# GNU comparison of sed: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
printf 'apple pie\nbanana split\ncherry tart\napple crumble\n\nplum cake\naab abb aaa\nlast line\n' > s1.txt
printf '가나다 abc\nÉcole école\nxyz\n한글 text\n' > s2.txt
printf 'one\r\ntwo\r\nthree\r\n' > s3.txt
SED_FILES="s1.txt words.txt lines.txt nonl.txt empty.txt blank.txt dup.txt"

compare_sed() { add_case out U - sed "$@"; }

SED_SCRIPTS=('p' 'd' '2d' '2,4d' '$d' '2!d' '1~2d' '2~3p' '0~2d' '/a/d' '/a/,/e/d' '/^$/d' '2,+1d' '2,~4d'
    '0,/a/d' '1,/a/d' '/b/,+1p' '$!N;P;D' 'N;N;s/\n/+/g' 's/a/X/' 's/a/X/g' 's/a/X/2' 's/a/X/2g'
    's/[aeiou]/<&>/g' 's/\(.\)\(.\)/\2\1/' 's/^/> /' 's/$/ </' 's/x*/-/g' 's/b*/X/g' 's/.*/\U&/'
    's/\w\+/\u&/g' 's/\(a\)\|b/[\1]/g' 's/a/\n/' 's/ /\t/g' 'y/abc/xyz/' 'y/ab/\n\t/' '=' 'l' 'l 5' 'l 0'
    'a foo' 'i bar' 'c X' '2c X' '2,3c X' 'x' 'G' 'H;$!d;x' 'h;G' 'g' '1h;1!G;$!d' '$!{h;d};x;G' 'n;d'
    'q' '2q' 'Q' '3Q' '2q5' 'z' 'w /dev/stdout' 's/a/b/w /dev/stdout' 's/a/b/p' 's/a/b/gp' 'p;p'
    '/e/!s/a/b/' ':a;s/aa/a/;ta' '/a/{s//X/;}' 's/a/b/;T;s/$/!/' 'b end;s/a/X/;:end'
    ':a;N;$!ba;s/\n/,/g' 'N;P;D' '$!N;s/\n/ /' 's/APPLE/x/I' '/APPLE/Id' 's/\bp/P/g' 's/p\b/P/g'
    's/a\+/X/g' 's/a\{2\}/X/g' 's/[[:upper:]]/U/g' 's/./&&/' 's/\./!/g' 's|a|/|g' 's,a,\,,g' 's/a/\&/g'
    '/a/s/p/P/' '2,3s/^/#/' '$s/$/ END/' '1!G;h;$!d' 's/\(^\|[^a-z]\)a/\1A/g' '/^$/,$d' '/a/,3p'
    'N;l' 'x;p;x' '1d;n;d' '$!d' '2{p;p}' '2,3{s/a/b/;p}' '/apple/{n;s/^/>/}' '2,$!d' 's/a/b/3' 's/\n//' 'D'
    '/b/,/c/{=;p}' '1,3!s/^/- /' 's/\(a*\)b/<\1>/' 's/^\(.\{3\}\).*/\1/' 'y/abcdefghij/ABCDEFGHIJ/')

for script in "${SED_SCRIPTS[@]}"; do
    for f in $SED_FILES; do
        compare_sed -e "$script" $f
        compare_sed -n -e "$script" $f
    done
done

ERE_SCRIPTS=('s/(a|b)+/X/g' 's/(.)\1/<\1>/g' 's/a{2,}/X/' 's/a?b/X/g' 's/(^| )a/\1A/g' '/^(apple|plum)/d' 's/([a-z]+) ([a-z]+)/\2 \1/'
    's/(a)(b)?/[\2]/' '/a+b/p' 's/[0-9]+/<&>/g' 's/^.{3}//')
for script in "${ERE_SCRIPTS[@]}"; do
    for f in s1.txt words.txt dup.txt; do
        compare_sed -E -e "$script" $f
        compare_sed -r -n -e "$script" $f
    done
done

# UTF-8: a character is a character
for script in 's/./X/' 's/./X/g' 'y/가나/AB/' 's/[가-힣]/H/g' 's/.*/\U&/' 's/É/e/I' 's/\(.\)\(.\)/\2\1/' 'l' '/한/d' 's/é/E/gI'; do
    compare_sed -e "$script" s2.txt
done

# options and several files
compare_sed -n '$p' s1.txt words.txt
compare_sed -s -n '$p' s1.txt words.txt
compare_sed -s -n '1p' s1.txt words.txt
compare_sed -n '1p;$=' s1.txt words.txt
compare_sed -s '1F' s1.txt words.txt
compare_sed -n -s '$=' s1.txt lines.txt
compare_sed -z 's/\n/,/g' s1.txt
compare_sed -z -n 'p' words.txt
compare_sed -n -l 20 l s1.txt
compare_sed --expression='s/a/b/' --expression='s/b/c/' s1.txt
compare_sed -e 'a\' -e 'text' s1.txt
compare_sed -e '1i\' -e '  indented' s1.txt
compare_sed '2{;p;}' s1.txt
compare_sed -n -e '/apple/{p;q}' s1.txt
compare_sed -f nosuch.sed s1.txt
compare_sed p nosuch.txt
compare_sed p nosuch.txt s1.txt
compare_sed -n '#n
p' s1.txt
compare_sed '#n
p' s1.txt
compare_sed -e 'r lines.txt' -e '2q' s1.txt
compare_sed '2r lines.txt' s1.txt
compare_sed 'R lines.txt' s1.txt
compare_sed -n 'W /dev/stdout' s1.txt
compare_sed 's/a/X/;ta;s/$/!/;:a' s1.txt
compare_sed '/a/{N;N;s/\n/|/g}' s1.txt
compare_sed -s -n '0~2p;$=' s1.txt words.txt

# errors: the status counts
for bad_script in 's/a/b' 'k' '{p' 'p}' 'b nolabel' '0p' 's/\(a/b/' 's/a/\1/' 'y/ab/c/' 'a' 's/a/b/gg' 's/a/b/pp' 's/a/b/0' 'p p' ',p' '1,p' '2,1!' 's/a/b/z' ':' 'r' 'w' '1,2=' '0,5p' '0~3p' 'y/a/' '/a' '\' 's' 'u'; do
    compare_sed "$bad_script" s1.txt
done
