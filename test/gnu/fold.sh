# GNU comparison of fold: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
printf 'a\tb\tc\n\bx\r\nabc\bd\nlong line with a tab\tand more words to wrap around the width\n\n\nspaces   between   words   and  more\n' > f_spec.txt
printf 'no newline at the end of a rather long line that has to wrap' > f_nonl.txt
printf 'wordwithoutanyspacesthatisverylongindeedandhastobebrokenanywhereitdoesnotmatter and then some\n' > f_long.txt
printf '   leading blanks and trailing blanks   \n\t\ttabs only then text that goes on and on\n' > f_blank.txt
printf 'a b c d e f g h i j k l m n o p q r s t u v w x y z a b c d e f g h i j k l m n o p q r s t u v w x y z\n' > f_alpha.txt
printf 'one\r\ntwo three four five six seven eight nine ten\r\n' > f_crlf.txt
for f in words.txt nonl.txt tabs.txt lines.txt empty.txt blank.txt dup.txt fields.txt f_spec.txt f_nonl.txt f_long.txt f_blank.txt f_alpha.txt f_crlf.txt big.txt; do
    compare_stdin "$f" fold
    for w in 1 2 3 5 8 10 13 20 40 80; do
        compare_stdin "$f" fold -w "$w"
        compare_stdin "$f" fold -s -w "$w"
        compare_stdin "$f" fold -b -w "$w"
        compare_stdin "$f" fold -bs -w "$w"
    done
done
compare_stdin f_alpha.txt fold -5
compare_stdin f_alpha.txt fold -s -12
compare_stdin f_alpha.txt fold -w5 -s
compare_stdin f_alpha.txt fold --width=7 --spaces
compare_stdin f_alpha.txt fold --width 9 --bytes
compare fold -w 10 f_alpha.txt f_nonl.txt f_long.txt
compare fold -w 10 f_nonl.txt f_alpha.txt
compare fold -w 0 f_alpha.txt
compare fold -w x f_alpha.txt
compare fold -w -3 f_alpha.txt
compare fold nosuchfile
compare fold -w 5 nosuchfile f_alpha.txt
compare fold -w
compare fold --bogus
compare fold -x
add_case status C - fold --help
add_case status C - fold --version
