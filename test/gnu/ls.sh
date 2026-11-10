# GNU comparison of ls: the formats, the sorting, the filters, the quoting styles, the indicators, the colours and the
# long format. What is Windows' own is left out of the long format by ls_long: the number of links, the owner and the group
# (the MSYS ones are mapped numbers) and the blocks. The names are lower case only, because ls sorts without regard to case
# (the Windows way); the names are bytes of ASCII, because the MSYS locale counts bytes.
set -f
mkdir -p ls_dir/sub ls_dir/zdir ls_dir/other
printf 'abc' > ls_dir/a.txt
printf 'x' > ls_dir/b.c
printf 'hello\n' > ls_dir/c.sh
head -c 2000 /dev/zero > ls_dir/big.dat
head -c 1500000 /dev/zero > ls_dir/huge.bin
: > ls_dir/empty
for n in ver1.txt ver2.txt ver10.txt ver01.txt x.tar.gz y.tar noext .hid bk~ .hid~; do printf 'x' > "ls_dir/$n"; done
printf 'x' > "ls_dir/sp ace"
printf 'x' > "ls_dir/it's"
printf 'x' > 'ls_dir/#hash'
printf 'x' > 'ls_dir/e=f'
printf 'x' > 'ls_dir/~tilde'
printf 'x' > ls_dir/sub/inner.txt
printf 'x' > 'ls_dir/sub/in ner.txt'
touch -d '2020-01-02 03:04:05' ls_dir/a.txt
touch -d '2021-05-06 07:08:09' ls_dir/b.c
touch -d '2019-12-31 23:59:59' ls_dir/c.sh
touch -d '2020-06-15 12:00:00' ls_dir/big.dat ls_dir/sub
touch -d '2022-02-02 02:02:02' ls_dir/huge.bin ls_dir/zdir
touch -d '2018-03-04 05:06:07' ls_dir/empty ls_dir/ver1.txt ls_dir/ver2.txt
mkdir -p ls_names
for n in plain 'a b' "it's" '#hash' '~tilde' 'e=f' 'x#y' 'tab,here' 'a$b' 'a`b' 'a;b' 'a&b' 'a(b' 'a[b' 'a!b' 'a^b' 'a%b' 'a{b' 'a~b' 'a@b' 'a+b' 'a-b' 'a_b' 'a  b' ' lead' 'trail '; do
    printf 'x' > "ls_names/$n" 2>/dev/null
done

# the long format without what is the system's own
ls_long() {
    local erc grc
    total=$((total + 1))
    gnu ls "$@" > exp.raw 2> exp.err < /dev/null; erc=$?
    ours ls "$@" > got.raw 2> got.err < /dev/null; grc=$?
    sed -E 's/^total .*/total/; s/^([^ ]{10}) +[0-9]+ +[^ ]+ +[^ ]+ +/\1 /' exp.raw > exp.out
    sed -E 's/^total .*/total/; s/^([^ ]{10}) +[0-9]+ +[^ ]+ +[^ ]+ +/\1 /' got.raw > got.out
    if ! cmp -s exp.out got.out || [ $erc != $grc ]; then
        bad=$((bad + 1))
        echo "DIFFERS: ls $* (status $erc, ours $grc)"
    fi
}

# the formats and the filters
for o in "" -1 -C -x -m -a -A -F -p -r -R -d "-F -R" "-a -r" "-a -F" "-1 -a -F" "-m -F" "-x -F" "-C -F" "-C -r" "-x -r" "-m -r" \
         --file-type "--file-type -R" --indicator-style=slash --indicator-style=classify --indicator-style=file-type \
         --indicator-style=none "--indicator-style=slash -a" -B "-B -a" "-I *.txt" "-I *.txt -I *.c" "--ignore=ver*" "--hide=*.txt" \
         "--hide=*.txt -a" "--hide=*.txt -A" "-B -I sub" --format=single-column --format=vertical --format=across --format=horizontal \
         --format=commas "--format=across -a" "-w 20 -C" "-w 20 -x" "-w 20 -m" "-w 40 -C" "-w 40 -x" \
         "-w 40 -m" "-w 5 -C" "-w 1 -m" "-w 0 -C" "-w 0 -m" "-w 0 -x" "-T 4 -C -w 50" "-T 0 -C -w 50" "-T 3 -x -w 30" "--width=33 -C" \
         "--tabsize=2 -C -w 33" "-C -w 100" "-x -w 100" "-m -w 100" "-C -w 60 -F" "-x -w 60 -F" "-C -w 60 -r"; do
    compare ls $o ls_dir
    compare ls $o ls_dir/sub
done
compare ls -d ls_dir ls_dir/sub ls_dir/a.txt
compare ls ls_dir/a.txt ls_dir/sub ls_dir/b.c
compare ls -F ls_dir/a.txt ls_dir/sub ls_dir/b.c
compare ls -R ls_dir
compare ls -R -a ls_dir
compare ls -R ls_dir/sub ls_dir/zdir
set +f
compare ls -d ls_dir/*
compare ls -d -F ls_dir/*
set -f
compare ls ls_dir/nosuchfile
compare ls ls_dir/nosuchfile ls_dir/a.txt
compare ls ls_dir ls_dir/nosuchfile ls_dir/sub

# the sorting (the times are those set above)
for o in -t -tr -S -Sr -U -v -vr -X -Xr -t1 "-S -t" "-t -S" "-U -t" "-t -U" --sort=none --sort=size --sort=time --sort=version \
         --sort=extension --sort=s --sort=t "--sort=size -r" "--sort=time -r" --group-directories-first "--group-directories-first -r" \
         "--group-directories-first -t" "--group-directories-first -S" "-f" "-fr" "-f -1" "-a -t" "-a -S" "-A -v" "-X -a"; do
    compare ls $o ls_dir
done
# the birth time is when the files were made: every one at the same moment is not told apart, so only the order of sizes is
compare ls --time=mtime ls_dir

# the quoting
for o in "" -b -q -Q -N --quoting-style=literal --quoting-style=shell --quoting-style=shell-always --quoting-style=shell-escape \
         --quoting-style=shell-escape-always --quoting-style=c --quoting-style=escape --quoting-style=locale --quoting-style=clocale \
         --quoting-style=c-maybe "-q -Q" "-Q -N" "-N -Q" "-b -N" "-N -b" "-Q -b" --show-control-chars "-q --show-control-chars" \
         "--show-control-chars -q" "-F --quoting-style=shell" "-F --quoting-style=shell-escape" "-F -Q" "-F -b" \
         "-C -w 80 --quoting-style=shell" "-x -w 80 --quoting-style=shell" "-m --quoting-style=shell" "-1 --quoting-style=shell" \
         "-C -w 80 --quoting-style=shell-escape" "-C -w 80 --quoting-style=shell-always" "-C -w 80 -Q" "-C -w 80 -b" "-C -w 80 -q" \
         "-x -w 40 --quoting-style=shell-escape" "-m -w 40 --quoting-style=shell-escape" "-R --quoting-style=shell" "-R -Q" "-R -b"; do
    compare ls $o ls_names
    compare ls $o ls_dir
    compare ls $o -R ls_dir
done
compare ls -d "ls_dir/sp ace" "ls_dir/it's" --quoting-style=shell
ls_long -l --quoting-style=shell ls_names
ls_long -l --quoting-style=shell-escape ls_names
ls_long -l -Q ls_names
ls_long -l -b ls_names
ls_long -l -q ls_names
ls_long -l ls_names

# the colours (LS_COLORS is set, as the built-in ones differ: the executables of Windows are those with a program extension)
export LS_COLORS='di=34:ln=36:*.txt=31:*.c=1;32:*.tar=33:*.gz=35:*~=90:fi=0'
for o in "--color=always" "--color=always -F" "--color=always -a" "--color=always -1" "--color=always -C -w 60" "--color=always -x -w 60" \
         "--color=always -m -w 60" "--color=always -R" "--color=always -r" "--color=always -d" "--color=always --quoting-style=shell" \
         "--color=always -Q" "--color=always -b" "--color=never" "--color=auto" "--color=yes" "--color=force" "--color=none" "--color=tty" \
         "--color=if-tty" "--color=no" "--color=al" "--color"; do
    compare ls $o ls_dir
    compare ls $o ls_dir/sub ls_dir/a.txt
done
ls_long -l --color=always ls_dir
ls_long -l --color=always -F ls_dir
export LS_COLORS='di=01;34:*.c=1;32'
compare ls --color=always ls_dir
export LS_COLORS='*.txt=7'
compare ls --color=always ls_dir
export LS_COLORS=
compare ls --color=always ls_dir
unset LS_COLORS
compare ls --color=always ls_dir/sub ls_dir/a.txt

# the long format
for o in -l "-l -a" "-l -A" "-l -F" "-l -p" "-l -r" "-l -t" "-l -S" "-l -h" "-l --si" "-lh -a" "-l -k" "-l --block-size=K" "-l --block-size=1" \
         "-l --block-size=1K" "-l --block-size=KB" "-l --block-size=KiB" "-l --block-size=M" "-l --block-size=MB" "-l --block-size=100" \
         "-l --block-size=2K" "-l --block-size=G" "-l -h --block-size=K" "-l --block-size=K -h" "-l -k -h" "-l -h -k" "-g" "-o" "-go" \
         "-l -g" "-l -o" "-l -G" "-lG" "-lgo" "-n" "-l -n" "-lnG" "-l --author" "-l --no-group" "-l -d" "-l -R" "-l -x" "-l -m" "-l -C" \
         "-l -1" "-1 -l" "-C -l" "-l --full-time" "--full-time" "-l --time-style=full-iso" "-l --time-style=long-iso" \
         "-l --time-style=iso" "-l --time-style=locale" "-l --time-style=posix-iso" "-l --time-style=posix-long-iso" \
         "-l --time-style=+%Y-%m-%d" "-l --time-style=+%H:%M:%S.%N" "-l --time-style=+%a|%A|%b|%B|%e|%d|%j|%m|%y|%Y|%%|%T|%F|%R|%D" \
         "-l --time-style=+%s" "-l --time-style=+%z" "-l --time-style=+%p|%I|%l|%k|%u|%w|%n|%t|%h" "-l --time-style=+old
new" \
         "-l --time-style=+%Q" "-l --time-style=+" "-l -t --time=mtime" "-l --sort=time" "-l --sort=size" "-l -X" "-l -v" "-l -U" \
         "-l --group-directories-first" "-l -I *.txt" "-l --hide=*.txt" "-l -B" "-l -a -B"; do
    ls_long $o ls_dir
    ls_long $o ls_dir/sub
done
ls_long --format=long ls_dir
ls_long --format=verbose ls_dir/sub
ls_long -l ls_dir/a.txt ls_dir/sub ls_dir/b.c
ls_long -l -d ls_dir ls_dir/sub
ls_long -l -R ls_dir
ls_long -l "--time-style=+old
new" ls_dir
ls_long -l ls_dir/nosuchfile
ls_long -l ls_dir/a.txt ls_dir/nosuchfile

# the inodes and the blocks: the inode of a file is the same number; the blocks are the space of Windows, a few files whose
# space does not depend on how the file system keeps small files (empty files and directories)
compare ls -i ls_dir
compare ls -i -1 ls_dir
compare ls -i -C -w 80 ls_dir
compare ls -i -R ls_dir/sub
compare ls -i -F ls_dir
compare ls -is ls_dir/zdir
compare ls -s ls_dir/zdir
compare ls -s -a ls_dir/other
compare ls -sk ls_dir/other
compare ls -sh ls_dir/other
compare ls -s --block-size=K ls_dir/other
compare ls -s --si ls_dir/other
compare ls -si ls_dir/other
compare ls -ls ls_dir/other

# the times: the access and change times are those of the moment, so the order of them is left alone
compare ls -t ls_dir/sub
compare ls -ct ls_dir/zdir ls_dir/other
compare ls --time=ctime -t ls_dir/zdir
compare ls --time=birth -t ls_dir/zdir
compare ls --time=status ls_dir/zdir
compare ls --time=creation ls_dir/zdir

# the errors of the options
for o in --sort=bad --sort= --format=bad --format=v --format=c --color=bad --indicator-style=bad --quoting-style=bad --time=bad --time=mtime \
         --time-style=bad --block-size=bad --block-size= --block-size=0 --block-size=1x --block-size=-1 --block-size=5KX "-w x" "-w -1" "-w 1x" \
         "-w" "-T x" "-T -1" "-T 1x" "-T" --width= --width=x --tabsize=x -I --hide --bogus -Z -D --color= --format= --indicator-style=; do
    compare ls $o ls_dir
done
compare ls -w 10x
compare ls --sort
compare ls --sort=ti ls_dir
compare ls --sort=no ls_dir
compare ls --sort=ver ls_dir
compare ls --sort=ex ls_dir
compare ls --sort=s ls_dir
compare ls --format=comm ls_dir
compare ls --format=sing ls_dir
compare ls --format=h ls_dir
compare ls --format=a ls_dir
compare ls --color=a ls_dir
compare ls --color=n ls_dir
compare ls --quoting-style=sh ls_dir
compare ls --quoting-style=shell-e ls_dir
compare ls --quoting-style=c ls_dir
compare ls --quoting-style=cl ls_dir
compare ls --quoting-style=l ls_dir
compare ls --indicator-style=s ls_dir
compare ls --indicator-style=f ls_dir
compare ls --indicator-style=c ls_dir
compare ls --indicator-style=n ls_dir
add_case status C - ls --help
add_case status C - ls --version
