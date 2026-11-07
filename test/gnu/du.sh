# GNU comparison of du, on a tree of files of known sizes (with sizes in bytes: the blocks of Windows are its own).
mkdir -p du_t/a/b du_t/c du_t/empty
head -c 5000 /dev/zero > du_t/f1
head -c 100 /dev/zero > du_t/a/f2
head -c 10000 /dev/zero > du_t/a/b/f3
head -c 1 /dev/zero > du_t/c/f4
head -c 123456 /dev/zero > du_t/big.bin
: > du_t/c/zero
ln du_t/f1 du_t/c/f1link 2> /dev/null
for opts in '' '-a' '-s' '-c' '-ac' '-sc' '-d0' '-d1' '-d2' '-ad1' '-S' '-aS' '-x' '-L' '-P' '-0' '--total' '--max-depth=1' '--summarize' '-l' '-al'; do
    compare du -b $opts du_t
    compare du --apparent-size -B1 $opts du_t
    compare du --apparent-size -k $opts du_t
    compare du --apparent-size -h $opts du_t
    compare du --apparent-size --si $opts du_t
    compare du --apparent-size -m $opts du_t
done
compare du -b du_t/f1
compare du -b du_t/f1 du_t/a
compare du -bc du_t/f1 du_t/a du_t/c
compare du -bs du_t/a du_t/c du_t/f1
compare du -b du_t/a du_t
compare du -bc du_t du_t/a
compare du -b du_t/f1 du_t/f1
compare du -bc du_t/f1 du_t/f1
compare du -b -B 1K du_t
compare du -b -B K du_t
compare du -b -B 1000 du_t
compare du -b -BKB du_t
compare du -b -BM du_t
compare du -b -B 4096 du_t
compare du --apparent-size -b -h du_t
compare du -b --exclude='*.bin' du_t
compare du -ab --exclude='f*' du_t
compare du -ab --exclude=a du_t
compare du -b nosuchfile
compare du -b du_t nosuchfile du_t/a
compare du -bd x du_t
compare du -bd -1 du_t
compare du -b -B 0 du_t
compare du -b -B x du_t
compare du -bs -d1 du_t
compare du --bogus
compare du -y
add_case status C - du --help
add_case status C - du --version
