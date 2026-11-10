# GNU comparison of pr: pages, headers, columns (down, across, merged), numbering, separators, widths, tabs, form feeds. The
# header holds the time of the file, so the files are named, not read from standard input (whose header is the current time).
seq 1 130 > pr_seq.txt
seq 1 10 > pr_ten.txt
seq 1 5 > pr_five.txt
seq 21 33 > pr_other.txt
printf 'The quick brown fox\njumps over\n\n  the lazy dog\tand\tcat\nend of file here with a very long line that goes on and on past the width of the page easily\nx\n' > pr_mix.txt
printf 'ab\tcd\nx\ny\nabcdefg\th\n\tindented\n\n\n   spaces   inside   here   \n' > pr_tabs.txt
printf 'a\nb\n\fc\nd\n\f\fe\nf\n' > pr_ff.txt
printf 'caf\303\251 \001\002 ctl \177 del \033 esc\nline2\r\nline3\n' > pr_ctl.txt
printf '1\n2' > pr_nonl.txt
printf '' > pr_empty.txt
printf '\n\n\n' > pr_blank.txt
touch -d '2020-03-04 05:06:07' pr_seq.txt pr_ten.txt pr_five.txt pr_other.txt pr_mix.txt pr_tabs.txt pr_ff.txt pr_ctl.txt pr_nonl.txt pr_empty.txt pr_blank.txt
texts="pr_seq pr_ten pr_five pr_mix pr_tabs pr_ff pr_ctl pr_nonl pr_empty pr_blank"

# one column: pages, lengths, numbering, margins, double spacing, header and trailer
for f in $texts; do
    compare pr $f.txt
    compare pr -t $f.txt
    compare pr -T $f.txt
    compare pr -n $f.txt
    compare pr -n -t $f.txt
    compare pr -d $f.txt
    compare pr -d -t $f.txt
    compare pr -f $f.txt
    compare pr -F -l 20 $f.txt
    compare pr -o 5 $f.txt
    compare pr -o 5 -n $f.txt
    compare pr -h HEADER $f.txt
    compare pr -h '' $f.txt
    compare pr -w 40 $f.txt
    compare pr -W 30 $f.txt
    compare pr -W 30 -n $f.txt
    compare pr -e $f.txt
    compare pr -e4 $f.txt
    compare pr -i $f.txt
    compare pr -c $f.txt
    compare pr -v $f.txt
    compare pr -l 12 $f.txt
    compare pr -l 12 -d $f.txt
    compare pr -l 12 -f $f.txt
    compare pr -l 12 -t $f.txt
    compare pr -l 9 $f.txt
    compare pr -l 10 $f.txt
    compare pr -l 11 $f.txt
    compare pr -l 15 -n $f.txt
    compare pr -N 100 -n $f.txt
    compare pr -n:3 -t $f.txt
    compare pr -nx2 -t $f.txt
    compare pr -n,7 -t $f.txt
    compare pr -D '%H:%M' -l 12 $f.txt
done

# pages
for p in +1 +2 +3 +4 +5 +2:2 +2:3 +1:2 +3:5 +9; do
    compare pr -l 12 $p pr_seq.txt
    compare pr -l 12 -n $p pr_seq.txt
    compare pr -l 20 -t $p pr_ten.txt
    compare pr $p pr_seq.txt
done
compare pr --pages=2:3 -l 12 pr_seq.txt
compare pr -N 50 -n +2 -l 12 pr_seq.txt

# columns: down, across, with numbers, separators and widths. (-s together with -n in columns that are filled down, and tabs
# in numbered columns, are not compared: the position pr counts for them is not the one it prints.)
for f in pr_seq pr_ten pr_five pr_mix pr_tabs pr_ff pr_ctl pr_nonl pr_blank; do
    for c in 2 3 4 5; do
        compare pr -$c $f.txt
        compare pr -$c -t $f.txt
        compare pr -$c -a $f.txt
        compare pr -$c -a -t $f.txt
        [ $f = pr_tabs ] || compare pr -$c -n -t $f.txt
        [ $f = pr_tabs ] || compare pr -$c -a -n -t $f.txt
        compare pr -$c -d -t $f.txt
        compare pr -$c -a -d -t $f.txt
        compare pr -$c -s -t $f.txt
        [ $f = pr_tabs ] || compare pr -$c -s: -t $f.txt
        compare pr -$c -S'| ' -t $f.txt
        compare pr -$c -a -S'| ' -t $f.txt
        [ $f = pr_tabs ] || compare pr -$c -Sab -n -t $f.txt
        compare pr -$c -J -t $f.txt
        compare pr -$c -w 50 -t $f.txt
        compare pr -$c -W 50 -a -t $f.txt
        [ $f = pr_tabs ] || compare pr -$c -w 60 -n -t $f.txt
        compare pr -$c -s -w 60 -t $f.txt
        compare pr -$c -o 4 -t $f.txt
        [ $f = pr_tabs ] || compare pr -$c -o 8 -n -t $f.txt
        compare pr -$c -e -t $f.txt
        compare pr -$c -i,3 -t $f.txt
        compare pr -$c -l 14 $f.txt
        compare pr -$c -l 14 -a $f.txt
        compare pr -$c -l 20 -d $f.txt
        compare pr -$c -f -l 20 $f.txt
        compare pr -$c +2 -l 14 $f.txt
    done
done
compare pr --columns=3 --across --number-lines=:3 -t pr_ten.txt
compare pr --columns=3 --sep-string=:: -t pr_ten.txt

# files side by side
for a in pr_seq pr_five pr_mix pr_nonl pr_ff pr_blank; do
    for b in pr_ten pr_other pr_tabs pr_empty; do
        compare pr -m -t $a.txt $b.txt
        compare pr -m -n -t $a.txt $b.txt
        compare pr -m -d -t $a.txt $b.txt
        compare pr -m -s -t $a.txt $b.txt
        compare pr -m -s: -t $a.txt $b.txt
        compare pr -m -S'||' -t $a.txt $b.txt
        compare pr -m -J -t $a.txt $b.txt
        compare pr -m -w 50 -t $a.txt $b.txt
        compare pr -m -W 40 -n -t $a.txt $b.txt
        compare pr -m -o 3 -t $a.txt $b.txt
        compare pr -m -l 14 $a.txt $b.txt
        compare pr -m -f -l 14 $a.txt $b.txt
        compare pr -m $a.txt $b.txt
    done
    compare pr -m -t $a.txt pr_ten.txt pr_five.txt
    compare pr -m -n -t $a.txt pr_ten.txt pr_five.txt
    compare pr -m -s -t $a.txt pr_ten.txt pr_five.txt
done

# several files, one after the other, errors
compare pr pr_ten.txt pr_five.txt
compare pr -n -t pr_ten.txt pr_five.txt
compare pr -h Both pr_ten.txt pr_five.txt
compare pr pr_ten.txt nosuchfile_zz pr_five.txt
compare pr -r nosuchfile_zz pr_five.txt
compare pr -t nosuchfile_zz
compare pr -m -t pr_ten.txt nosuchfile_zz
compare pr -3 -m pr_ten.txt pr_five.txt
compare pr -l x pr_ten.txt
compare pr -l 0 pr_ten.txt
compare pr -w 0 pr_ten.txt
compare pr -o x pr_ten.txt
compare pr -N x pr_ten.txt
compare pr -3 -w 3 pr_ten.txt
compare pr -0 pr_ten.txt
compare pr -n,0 pr_ten.txt
compare pr -e,,3 pr_ten.txt
compare pr +x pr_ten.txt
compare pr +1: pr_ten.txt
compare pr --bogus
