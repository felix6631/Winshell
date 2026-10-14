# GNU comparison of sort: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for f in $TEXTS; do
    for o in "" -r -u -f -b -d -n -rn -nu -s -fu "-k2" "-k1,1" "-k2,2n" "-k2,2nr" "-k1.2" "-k1.2,1.3" \
             "-t: -k2n" "-t: -k2,2n -k1,1r" "-t: -k3 -k1" "-ru" "-c" "-C" "-g" "-i" "-t: -u -k3,3" "-k2b" "-k 2,2 -s"; do
        compare sort $o $f
    done
done
compare sort words.txt nums.txt
compare_now sort -o out_file.txt words.txt
compare sort nosuch.txt
compare sort -k0 words.txt
