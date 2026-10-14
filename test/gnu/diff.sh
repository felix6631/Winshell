# GNU comparison of diff: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# Timestamps differ per file: the unified and context formats are compared with --label.
printf 'one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\n' > dA.txt
printf 'one\ntwo\n3\nfour\nfive\nsix\nseven\neight\nnine\nten\neleven\n' > dB.txt
printf 'zero\none\ntwo\nthree\nfour\nfive\nsix\nSEVEN\neight\nnine\n' > dC.txt
printf 'a b\n  c\n\nd\n' > dD.txt
printf 'A  B\nc \n\n\nD\n' > dE.txt
printf 'x\ny\nz' > dNo.txt
printf 'x\ny\nz\n' > dNl.txt
printf 'a\0b\n' > dBin.bin
printf 'a\0c\n' > dBin2.bin
printf 'a\r\nb\r\nc\r\n' > dCr.txt
printf 'a\nb\nc\n' > dLf.txt
mkdir -p dt1/sub dt1/same dt2/sub dt2/same
printf 'x\n' > dt1/only1.txt; printf 'y\n' > dt2/only2.txt
printf 'same\n' > dt1/same.txt; printf 'same\n' > dt2/same.txt
printf 'a\nb\n' > dt1/sub/f.txt; printf 'a\nc\n' > dt2/sub/f.txt
printf 'm\n' > dt1/m.txt; printf 'n\n' > dt2/m.txt
printf 'i\n' > dt1/same/i.txt; printf 'i\n' > dt2/same/i.txt

for pair in "dA.txt dB.txt" "dA.txt dC.txt" "dB.txt dC.txt" "dC.txt dA.txt" "dD.txt dE.txt" "dNo.txt dNl.txt" "dNl.txt dNo.txt" \
            "empty.txt dA.txt" "dA.txt empty.txt" "dA.txt dA.txt" "empty.txt empty.txt" "dBin.bin dBin2.bin" "dCr.txt dLf.txt" \
            "words.txt dup.txt" "lines.txt words.txt" "nums.txt fields.txt" "table.txt tabs.txt" "nonl.txt words.txt" "blank.txt dD.txt"; do
    set -- $pair
    for o in "" "-u" "-U0" "-U1" "-c" "-C0" "-C1" "-q" "-s" "-i" "-w" "-b" "-B" "-a" "-uw" "-ui" "-u --strip-trailing-cr" "-N"; do
        eval "set -- $o $pair"
        add_case out C - diff --label x --label y "$@"
    done
done
add_case out C - diff --label x --label y --unified=2 dA.txt dC.txt
add_case out C - diff --label x --label y --context=1 dA.txt dB.txt
add_case out C - diff --label x --label y -2 dA.txt dB.txt
add_case out C - diff --label x --label y --normal dA.txt dB.txt
add_case out C - diff --brief dA.txt dB.txt
add_case out C - diff --report-identical-files dA.txt dA.txt
add_case out C dA.txt diff - dB.txt
add_case out C dB.txt diff dA.txt -
add_case out C - diff dt1 dt2
add_case out C - diff -r dt1 dt2
add_case out C - diff -rq dt1 dt2
add_case out C - diff -rN dt1 dt2
add_case out C - diff -r -x sub dt1 dt2
add_case out C - diff -r -x '*.txt' dt1 dt2
add_case out C - diff dt1/sub/f.txt dt2/sub/f.txt
add_case out C - diff dt1 dA.txt
add_case out C - diff dA.txt dt1
add_case status C - diff dA.txt nosuch.txt
add_case status C - diff nosuch.txt dA.txt
add_case status C - diff -N nosuch.txt dA.txt
add_case status C - diff dA.txt
add_case status C - diff dA.txt dB.txt dC.txt
add_case status C - diff --bogus dA.txt dB.txt
add_case status C - diff -U x dA.txt dB.txt
