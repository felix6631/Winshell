# GNU comparison of pathchk: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# Only the exit codes matter (the messages go to standard error). The limits of the file system are not compared:
# the MSYS tool takes them from Cygwin, this one uses the Linux values (255 and 4095).
L14=aaaaaaaaaaaaaa
L15=aaaaaaaaaaaaaaa
L255=$(printf 'a%.0s' $(seq 1 255))
L256=$(printf 'a%.0s' $(seq 1 256))
P=$(printf 'ab/%.0s' $(seq 1 90))
Q=$(printf 'ab/%.0s' $(seq 1 120))
mkdir -p pc_dir
printf x > pc_dir/file
for name in a a/b /a /a/b a//b a/ ./a ../a . .. / // -a a/-b -- "a b" a:b 'a*b' 'a	b' "$L14" "$L15" "$L255" "$L256" "a/$L15" "$L15/a" \
            "$P" "$Q" pc_dir pc_dir/ pc_dir/file pc_dir/file/x pc_dir/nosuch pc_dir/nosuch/deep /nosuchdir/x é "a/é" 'a\b' a.b a_b a-b A1; do
    for o in "" -p -P -pP --portability; do
        [ "$name" = -- ] && continue
        add_case out C - pathchk $o -- "$name"
    done
done
add_case out C - pathchk ''
add_case out C - pathchk -p ''
add_case out C - pathchk -P ''
add_case out C - pathchk --portability ''
add_case out C - pathchk a b c
add_case out C - pathchk a "$L256" c
add_case out C - pathchk -p a "$L15" c
add_case out C - pathchk -p a '' c
add_case out C - pathchk
add_case out C - pathchk -p
add_case out C - pathchk -pP
add_case out C - pathchk --bogus a
add_case out C - pathchk -x a
add_case status C - pathchk --help
add_case status C - pathchk --version
