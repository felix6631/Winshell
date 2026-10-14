# GNU comparison of find: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
mkdir -p ftree/src/lib ftree/docs ftree/empty_dir ftree/.hidden ftree/deep/er/est
printf 'int main(){}\n' > ftree/src/main.c
printf 'void f(){}\n' > ftree/src/lib/util.c
printf '#define X\n' > ftree/src/lib/util.h
printf 'hello\n' > ftree/docs/README.md
printf 'notes notes notes notes notes\n' > ftree/docs/notes.TXT
printf '' > ftree/docs/zero.txt
printf 'secret\n' > ftree/.hidden/key
printf 'x' > ftree/deep/er/est/bottom.txt
head -c 3000 /dev/zero > ftree/big.bin
touch -d '2020-01-01 00:00:00' ftree/old.txt
touch -d '2021-06-15 12:00:00' ftree/docs/notes.TXT
touch ftree/new.txt
touch -d '2 days ago' ftree/two_days.txt
touch -d '3 hours ago' ftree/hours.txt

compare_find() { add_case out U - find "$@"; }

compare_find ftree
compare_find ftree/src ftree/docs
compare_find ftree -name '*.c'
compare_find ftree -name '*.[ch]'
compare_find ftree -iname '*.txt'
compare_find ftree -name 'README*' -o -name 'util*'
compare_find ftree -name '?ain.c'
compare_find ftree -name '.*'
compare_find ftree -path '*/lib/*'
compare_find ftree -ipath '*/DOCS/*'
compare_find ftree -path 'ftree/src*' -name '*.c'
compare_find ftree -type f
compare_find ftree -type d
compare_find ftree -type f -name '*.txt'
compare_find ftree -type d -empty
compare_find ftree -type f -empty
compare_find ftree -empty
compare_find ftree -maxdepth 0
compare_find ftree -maxdepth 1
compare_find ftree -maxdepth 2 -type f
compare_find ftree -mindepth 2
compare_find ftree -mindepth 2 -maxdepth 3
compare_find ftree -depth
compare_find ftree -depth -type d
compare_find ftree -size 0
compare_find ftree -size -1
compare_find ftree -size +1k
compare_find ftree -size 3000c
compare_find ftree -size +2000c -size -4000c
compare_find ftree -size 1
compare_find ftree -size 6
compare_find ftree -newer ftree/old.txt -type f
compare_find ftree -newer ftree/new.txt
compare_find ftree -mtime +1000 -type f
compare_find ftree -mtime 0 -type f
compare_find ftree -mtime -1 -type f
compare_find ftree -mtime 2 -type f
compare_find ftree -mtime +1 -type f
compare_find ftree -mmin -1 -type f
compare_find ftree -mmin +60 -mmin -300 -type f
compare_find ftree -regex '.*\.c'
compare_find ftree -regex 'ftree/src/.*'
compare_find ftree -regextype posix-extended -regex '.*/(main|util)\.[ch]'
compare_find ftree -regex '.*\(main\|util\)\.c'
compare_find ftree -iregex '.*readme.*'
compare_find ftree -regex '.*lib'
compare_find ftree -prune
compare_find ftree -name lib -prune -o -type f -print
compare_find ftree -name docs -prune -o -name '*.txt' -print
compare_find ftree -type f -name '*.c' -print
compare_find ftree -type f -print0
compare_find ftree -type f -name '*.c' -o -name '*.h'
compare_find ftree '(' -name '*.c' -o -name '*.h' ')' -type f
compare_find ftree ! -name '*.txt' -type f
compare_find ftree -not -type d
compare_find ftree -type f -a -name '*.md'
compare_find ftree -name main.c , -name README.md
compare_find ftree -true
compare_find ftree -false
compare_find ftree -name '*.c' -quit
compare_find ftree -type f -printf '%p|%f|%h|%s|%d|%y\n'
compare_find ftree -maxdepth 1 -printf '%P\n'
compare_find ftree/src -printf '%P\n'
compare_find ftree/src/ -printf '%p\n'
compare_find ftree -type f -name '*.c' -exec echo {} ';'
compare_find ftree -type f -name '*.c' -exec echo found {} +
compare_find ftree -type f -name '*.c' -exec echo a{}b {} ';'
compare_find ftree -type f -name '*.c' -exec false ';' -print
compare_find ftree -type f -name '*.c' -exec true ';' -print
compare_find ftree nosuch
compare_find nosuch ftree/src
compare_find ftree -bogus
compare_find ftree -name
compare_find ftree -size abc
compare_find ftree -maxdepth x
compare_find ftree -type x
compare_find ftree '(' -name x
compare_find ftree -name x ')'
compare_find ftree -exec echo
compare_find ftree -regex '\('
compare_find ftree/src/main.c ftree/docs/README.md
compare_find ftree/src/main.c -name main.c
compare_find ftree/.hidden
compare_find ftree -name '*가*'

