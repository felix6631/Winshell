# GNU comparison of fmt: texts of several kinds against many widths, goals and the modes (-u -s -c -t -p).
printf 'The quick brown fox jumps over the lazy dog. This is a longer sentence that has to be wrapped somewhere.  And a second one!  Third? Yes.\n\nSecond paragraph here, with  odd   spacing.\n   Indented   line one\n   indented line two\n' > fm_basic.txt
printf 'Mr. Smith went to Washington.  He said "Hello there."  (Parenthesis (nested) ends here.)  Then [brackets] and `quotes'"'"' and "double quotes." appear; semi-colons, commas, and colons: all matter!  Dr. Who? No one knows... Perhaps e.g. this. Or i.e. that. End.\n' > fm_punct.txt
printf 'a b c d e f g h i j k l m n o p q r s t u v w x y z a b c d e f g h i j k l m n o p q r s t u v w x y z a b c d e f g h i j k l m n o p q r s t u v w x y z\n' > fm_short.txt
printf 'Supercalifragilisticexpialidocious antidisestablishmentarianism pneumonoultramicroscopicsilicovolcanoconiosis and some short words follow them here.\n' > fm_long.txt
printf 'One line.\nSecond line of same paragraph.\nThird line.\n\n\n\nAfter blank lines. More text here to wrap around the width limit set by the option.\n\t\ttabbed paragraph with words that go on and on and on\n\t\tand continue on the next line as well.\n' > fm_lines.txt
printf '  indent two first line of a paragraph that goes on for a while\n    indent four second line follows with more words in it\n    indent four third line with some more words to fill the width\n  back to two\n' > fm_crown.txt
printf '> quoted text that is long enough to wrap around the margin of the page\n> and a second quoted line of the same paragraph\n>\n> new quoted paragraph here that is also long enough to need wrapping\nnot quoted line that should be left alone completely\n> after\n' > fm_quote.txt
printf '# comment line one that is long enough to be wrapped by the formatter\n# comment line two\n  # indented comment that is long enough to be wrapped by the formatter ok\n' > fm_hash.txt
printf 'tag: first line is a tag and the rest of the paragraph hangs below it indented\n     more of the hanging text that goes on and on for a while longer\n\nsame: first line then\nsame: second line with identical indent\n' > fm_tag.txt
printf 'no newline at end of this text which is long enough to wrap' > fm_nonl.txt
printf '\n\n  \n\t\nonly blank lines above and trailing spaces   \n   \n' > fm_blank.txt
printf '' > fm_empty.txt
printf 'word\n' > fm_one.txt
printf 'ends here.\nStarts a new sentence on a new line.  Also this!\nand lowercase after period.\nQuestion?\nAnswer.\n' > fm_sent.txt
printf 'a.b c.d e..f g!h (i) [j] "k" '"'"'l'"'"' m.) n.] o." p.'"'"' q?) r!"\n' > fm_marks.txt
widths="1 5 10 15 20 25 30 40 50 60 75 80"
for f in fm_basic fm_punct fm_short fm_long fm_lines fm_crown fm_quote fm_hash fm_tag fm_nonl fm_blank fm_empty fm_one fm_sent fm_marks; do
    add_case out C $f.txt fmt
    for w in $widths; do
        add_case out C $f.txt fmt -w $w
        add_case out C $f.txt fmt -w $w -u
        add_case out C $f.txt fmt -w $w -s
        add_case out C $f.txt fmt -w $w -c
        add_case out C $f.txt fmt -w $w -t
        add_case out C $f.txt fmt -w $w -c -u
        add_case out C $f.txt fmt -w $w -t -u
    done
    add_case out C $f.txt fmt -g 20 -w 40
    add_case out C $f.txt fmt -g 30
    add_case out C $f.txt fmt -g 1 -w 20
    add_case out C $f.txt fmt -w 40 -g 40
    add_case out C $f.txt fmt -30
    add_case out C $f.txt fmt -72 -u
    add_case out C $f.txt fmt --width=33 --goal=25 --uniform-spacing
    add_case out C $f.txt fmt --crown-margin --width=33
    add_case out C $f.txt fmt --tagged-paragraph --width=33
    add_case out C $f.txt fmt --split-only --width=33
done
for w in 20 30 50; do
    add_case out C fm_quote.txt fmt -p '>' -w $w
    add_case out C fm_quote.txt fmt -p '> ' -w $w
    add_case out C fm_quote.txt fmt -p ' >' -w $w
    add_case out C fm_quote.txt fmt -p '>' -w $w -u
    add_case out C fm_quote.txt fmt -p '>' -w $w -c
    add_case out C fm_quote.txt fmt -p '>' -w $w -t
    add_case out C fm_quote.txt fmt -p '>' -w $w -s
    add_case out C fm_hash.txt fmt -p '#' -w $w
    add_case out C fm_hash.txt fmt -p '# ' -w $w
    add_case out C fm_hash.txt fmt -p '  #' -w $w
    add_case out C fm_basic.txt fmt -p 'The' -w $w
    add_case out C fm_basic.txt fmt -p 'x' -w $w
    add_case out C fm_basic.txt fmt -p '' -w $w
done
add_case out C - fmt nosuchfile.txt
add_case out C - fmt fm_basic.txt nosuchfile.txt fm_one.txt
add_case out C fm_basic.txt fmt -
add_case out C fm_basic.txt fmt - -w 30
add_case out C - fmt -w
add_case out C - fmt -w x fm_basic.txt
add_case out C - fmt -w -5 fm_basic.txt
add_case out C - fmt -w 99999 fm_basic.txt
add_case out C - fmt -w 2490 fm_basic.txt
add_case out C - fmt -w 2491 fm_basic.txt
add_case out C - fmt -w 2500 fm_basic.txt
add_case out C - fmt -w 2501 fm_basic.txt
add_case out C - fmt -w 1073741823 fm_basic.txt
add_case out C - fmt -w 1073741824 fm_basic.txt
add_case out C - fmt -w 99999999999 fm_basic.txt
add_case out C - fmt -g 1073741824 fm_basic.txt
add_case out C - fmt -g x fm_basic.txt
add_case out C - fmt -g 100 -w 40 fm_basic.txt
add_case out C - fmt -g 40 -w 40 fm_basic.txt
add_case out C - fmt -g 41 -w 40 fm_basic.txt
add_case out C - fmt -w 40 -g 39 fm_basic.txt
add_case out C - fmt -x fm_basic.txt
add_case status C - fmt --bogus fm_basic.txt
add_case status C - fmt --help
add_case status C - fmt --version
# long paragraphs (the buffer of the paragraph is flushed), tabs, several files
seq 1 3000 | tr '\n' ' ' > fm_big.txt
printf '\n' >> fm_big.txt
seq 1 2500 | sed 's/$/./' | tr '\n' ' ' > fm_bigsent.txt
printf '\n' >> fm_bigsent.txt
for n in 800 1200 1500; do seq 1 $n | sed 's/^/word/' | paste -sd' ' > fm_n$n.txt; done
head -c 12000 /dev/zero | tr '\0' 'x' > fm_giant.txt
printf '\n' >> fm_giant.txt
printf '\tTabbed first line of a paragraph with\ttabs\tinside the text\n\tand a second tabbed line\twith more\n\n        eight spaces then tabs\there\n' > fm_tabs.txt
printf 'line one\r\nline two of the same paragraph\r\n\r\nnext one\r\n' > fm_crlf.txt
for f in fm_big fm_bigsent fm_n800 fm_n1200 fm_n1500 fm_giant fm_tabs; do
    add_case out C $f.txt fmt
    add_case out C $f.txt fmt -w 20
    add_case out C $f.txt fmt -w 60 -u
    add_case out C $f.txt fmt -s -w 40
    add_case out C $f.txt fmt -c
    add_case out C $f.txt fmt -t -w 33
done
add_case out C - fmt fm_basic.txt fm_one.txt fm_punct.txt
add_case out C - fmt -w 30 fm_tabs.txt fm_basic.txt
