# GNU comparison of numfmt: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# GNU computes with long double; numbers of 16 or more digits can differ in the last place, so none are used here.
nf_values="0 1 9 10 99 100 999 1000 1001 1049 1050 1051 1099 1100 1149 1150 9949 9950 9999 10000 10001 10400 10500 99999 999999 1000000 1234567 12345678 123456789 999999999 1000000000 1048576 1073741824 1099511627776 1.5 0.5 2.5 1.05 10.4 123.456 1000.5 0.4 9.5 99.95 999.5 .5 00012 1.0 1.50 -- abc 1e3 5. +5 1,000 1K 1k 1Ki 1M 1.5K 1.5Ki 2G 1KB 1KiB K 1P 1E 1Z 1Y 5.K"
for v in $nf_values; do
    [ "$v" = -- ] && continue
    for opt in "" --to=si --to=iec --to=iec-i --from=si --from=iec --from=iec-i --from=auto --from=none "--from=auto --to=si" "--from=iec --to=iec-i" "--round=up --to=si" "--round=down --to=si" "--round=nearest --to=si" "--round=towards-zero --to=si" "--round=nearest" "--padding=8" "--padding=-8" "--format=%.2f" "--format=%08.1f" "--suffix=B" "--to=si --suffix=B"; do
        add_case out C - numfmt $opt "$v"
    done
done
for v in -1 -1001 -1050 -10400 -0.5 -2.5 -999999 -1.5K; do
    for opt in "" --to=si --to=iec "--round=up --to=si" "--round=down --to=si" "--round=nearest --to=si" "--round=towards-zero --to=si" "--from=auto"; do
        add_case out C - numfmt $opt -- "$v"
    done
done

# lines: fields, delimiters, header, invalid
printf '1000 2000 3000\n' > nf_three.txt
printf '  1000   2000\n' > nf_blanks.txt
printf '1000\t2000\n' > nf_tab.txt
printf '1000,2000,3000\n' > nf_comma.txt
printf '1000,,3000\n' > nf_empty.txt
printf '1000, 2000\n' > nf_commaspace.txt
printf '1000,\n' > nf_trail.txt
printf 'a 1000 b\n' > nf_text.txt
printf 'h1\nh2\n1000\n2000\n' > nf_header.txt
printf 'a\n1000\nb\n2000\n' > nf_mixed.txt
printf '1000\n\n2000\n' > nf_blankline.txt
printf '1000 2000' > nf_nonl.txt
printf '5K 5M 5G\n1.5K 2.5Mi x\n' > nf_suffixes.txt
printf '1000\r\n2000\r\n' > nf_crlf.txt
printf '' > nf_none.txt
for f in nf_three.txt nf_blanks.txt nf_tab.txt nf_comma.txt nf_empty.txt nf_commaspace.txt nf_trail.txt nf_text.txt nf_header.txt nf_mixed.txt nf_blankline.txt nf_nonl.txt nf_none.txt; do
    for opt in --to=si "--to=si --field=2" "--to=si --field=2-" "--to=si --field=-2" "--to=si --field=1,3" "--to=si --field=-" "--to=si --field=1-2,2" "--to=si --field=3" "--to=si -d, --field=2" "--to=si -d, --field=1-" "--to=si -d, --field=3" "--to=si -d, --field=2 --invalid=ignore" "--to=iec --padding=10" "--to=si --padding=-10 --field=1-" "--to=si --header" "--to=si --header=2" "--to=si --invalid=ignore" "--to=si --invalid=warn" "--to=si --invalid=fail" "--to=si --invalid=abort" "--to=si --suffix=B" "--to=si -z"; do
        add_case out C $f numfmt $opt
    done
done
for f in nf_suffixes.txt; do
    for opt in "--from=auto --field=1-" "--from=si --field=1-" "--from=iec --field=1-" "--from=auto --to=iec --field=1- --invalid=warn" "--from=none --field=1- --invalid=fail" "--to=si --field=1- --invalid=warn"; do
        add_case out C $f numfmt $opt
    done
done
# options and their errors
for opt in "--field=0" "--field=x" "--field=2-1" "--field=" "--field=1,x" "--field=-0" "--field=1-x" "-d" "-dab" "-d," "--header=0" "--header=x" "--header=-1" "--header=1x" "--padding=0" "--padding=x" "--padding=1x" "--from=bogus" "--to=bogus" "--round=bogus" "--invalid=bogus" "--from=i" "--to=i" "--to=s" "--from=a" "--round=n" "--round=u" "--invalid=f" "--from-unit=0" "--from-unit=x" "--from-unit=2.5" "--from-unit=-1" "--to-unit=0" "--to-unit=K" "--to-unit=Ki" "--from-unit=2K" "--from-unit=18446744073709551615" "--from-unit=18446744073709551616" "--grouping --to=si" "--grouping --format=%f" "--grouping" "--format=%f" "--format=f" "--format=%%" "--format=%d" "--format=%e" "--format=%+f" "--format=%#f" "--format=%f%f" "--format=a%fb" "--format=%5f" "--format=%-5f" "--format=%05f" "--format=%.3f" "--format=%.0f" "--format=%5.1f" "--format=%'f" "--format=%0-5f" "--format=% f" "--format=%f%%" "--format=%%%f" "--format=" "--debug" "--debug --to=si" "--suffix=B --debug"; do
    add_case out C - numfmt $opt 1500
    add_case out C - numfmt $opt 1.25 2K
done
for opt in "--to=si --format=%10f" "--to=si --format=%-10f|" "--to=iec --format=%010f" "--to=si --format=%.3f" "--to=si --format=%.0f" "--to=si --format=%.5f" "--to=si --format=%5.1f" "--to=si --format=[%f]" "--to=iec --suffix=iB" "--to=si --suffix=B --padding=9" "--to=si --padding=3" "--to=si --format=%10f --padding=3" "--to-unit=2" "--to-unit=3" "--to-unit=1K --to=si" "--from-unit=2" "--from-unit=1Ki --to=si" "--to=si --to-unit=1000" "--round=down --to-unit=3" "--round=up --to-unit=3"; do
    for v in 1 5 10 999 1000 1500 1536 12345 123456789 1.05 2.5 1000000; do
        add_case out C - numfmt $opt $v
    done
done
add_case status C - numfmt --help
add_case status C - numfmt --version
add_case out C - numfmt
add_case out C - numfmt --bogus
