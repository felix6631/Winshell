# GNU comparison of ptx: the three output formats, references, widths and gaps, ignore and only lists, case folding, break
# characters, word and sentence expressions, and the traditional mode. (The order of equal keywords from different files is
# not compared: GNU orders them by the addresses of its buffers.)
printf 'The quick brown fox jumps over the lazy dog.\nA second line, with other words: foxes and dogs!\n' > px_a.txt
printf 'abc_def 123 x9y it'"'"'s a-b\n' > px_b.txt
printf 'one. two  three.  four\nfive?\n' > px_c.txt
printf 'the Cat the cat THE CAT a b A B\n' > px_d.txt
printf 'ref1 the cat sat\nref22 a dog ran\nref3 the end\n' > px_r.txt
printf 'say "hi" {x} $5 & 100%% #1 _u ^c ~t a\\b <a> | b\n' > px_s.txt
printf 'a\tb  c   d\nfoo-bar baz_qux x.y\n' > px_t.txt
printf 'Supercalifragilisticexpialidocious is long. Antidisestablishmentarianism too!  Short ones: a b c.\nNo end here' > px_l.txt
printf 'The index of a text lists every word with the words around it.  A second sentence follows here, with commas; semicolons; and more.\nIt can wrap over lines, and the context may run across a line end without trouble.  Done!\n' > px_p.txt
printf 'the\na\nof\nand\n' > px_ignore.txt
printf 'cat\nB\nword\nwords\nindex\n' > px_only.txt
printf -- '-_' > px_break.txt
texts="px_a px_b px_c px_d px_r px_s px_t px_l px_p"

for f in $texts; do
    compare ptx $f.txt
    compare ptx -w 40 $f.txt
    compare ptx -w 60 $f.txt
    compare ptx -w 100 $f.txt
    compare ptx -w 20 $f.txt
    compare ptx -g 1 $f.txt
    compare ptx -g 6 -w 50 $f.txt
    compare ptx -f $f.txt
    compare ptx -A $f.txt
    compare ptx -A -R $f.txt
    compare ptx -R -w 50 $f.txt
    compare ptx -O $f.txt
    compare ptx -O -w 40 $f.txt
    compare ptx -O -A $f.txt
    compare ptx -O -M zz $f.txt
    compare ptx -T $f.txt
    compare ptx -T -w 40 $f.txt
    compare ptx -T -A $f.txt
    compare ptx -T -M zz -F '...' $f.txt
    compare ptx -F '...' -w 40 $f.txt
    compare ptx -F '' -w 40 $f.txt
    compare ptx -F '<>' -O -w 40 $f.txt
    compare ptx -i px_ignore.txt $f.txt
    compare ptx -o px_only.txt $f.txt
    compare ptx -f -o px_only.txt $f.txt
    compare ptx -o px_only.txt -i px_ignore.txt $f.txt
    compare ptx -W '[a-z]+' $f.txt
    compare ptx -W '[a-z0-9]+' -f $f.txt
    compare ptx -S ';' -w 60 $f.txt
    compare ptx -S '[.]' $f.txt
    compare ptx -G $f.txt
    compare ptx -G -w 50 $f.txt
    compare ptx -G -A $f.txt
    compare ptx -G -T $f.txt
    compare ptx -G -F '...' -w 40 $f.txt
    compare ptx --format=roff --width=44 --gap-size=4 $f.txt
    compare ptx --format=tex --auto-reference $f.txt
done
compare ptx -r px_r.txt
compare ptx -r -O px_r.txt
compare ptx -r -T px_r.txt
compare ptx -r -R px_r.txt
compare ptx -r -w 40 px_r.txt
compare ptx -r -f px_r.txt
compare ptx -A -w 30 px_a.txt
compare ptx -b px_break.txt px_t.txt
compare ptx -b px_break.txt -w 50 px_t.txt
compare ptx -w 10 px_a.txt
compare ptx -w 1 px_a.txt
compare ptx -A px_a.txt px_b.txt px_c.txt
compare ptx -O -A px_r.txt px_c.txt
compare ptx -T px_a.txt px_s.txt
compare ptx -G px_a.txt px_b.txt
compare ptx px_empty_zz.txt
compare ptx -g 0 px_a.txt
compare ptx -g x px_a.txt
compare ptx -w 0 px_a.txt
compare ptx -w x px_a.txt
compare ptx -i nosuchfile_zz px_a.txt
compare ptx -o nosuchfile_zz px_a.txt
compare ptx -b nosuchfile_zz px_a.txt
compare ptx -W '\(' px_a.txt
compare ptx -S 'x*' px_a.txt
compare ptx -G px_a.txt px_b.txt px_c.txt
compare ptx --format=bogus px_a.txt
compare ptx --bogus
