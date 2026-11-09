# GNU comparison of install: each case works in a fresh directory with a few files and compares the messages, the status,
# what is left (names, modes as stat shows them, contents). Owners and strip are not compared.
in_run() {
    local who=$1 rc f
    shift
    rm -rf in_dir
    mkdir in_dir
    printf 'hello\n' > in_dir/a
    printf 'other\n' > in_dir/a2
    printf 'hello\n' > in_dir/same
    printf 'exe\n' > in_dir/p.exe
    mkdir in_dir/dd
    printf 'old\n' > in_dir/dd/a
    printf 'old b\n' > in_dir/b
    ( cd in_dir && MSYS2_ARG_CONV_EXCL="*" "$who" install "$@" > ../in_out.txt 2> ../in_err.txt ); rc=$?
    sed -e 's#^[^ :]*install:#install:#' in_out.txt
    echo '-- err'; tr -d '\r' < in_err.txt | sed -e 's#^[^ :]*install:#install:#' -e "s#^Try '[^ ]*install #Try 'install #"
    echo "-- rc $rc"
    ( cd in_dir && find . -mindepth 1 | sort | while IFS= read -r f; do
        if [ -f "$f" ]; then echo "$f $(stat -c %a "$f") $(cat "$f" | tr '\n' '|')"; else echo "$f dir"; fi
    done )
}
in_case() {
    local g o
    total=$((total + 1))
    g=$(in_run gnu "$@")
    o=$(in_run ours "$@")
    if [ "$g" != "$o" ]; then
        bad=$((bad + 1))
        echo "DIFFERS: install $*"
    fi
}
in_case a c
in_case -v a c
in_case a b
in_case -v a b
in_case a a2 dd
in_case -v a a2 dd
in_case -v a dd
in_case -v a dd/
in_case -t dd a a2
in_case -v -t dd a a2
in_case -T a c
in_case -T a dd
in_case -T a b
in_case -T a a2 c
in_case a
in_case
in_case a b c
in_case nosuch c
in_case a nosuch/c
in_case -D a nosuch/c
in_case -D -v a x/y/z
in_case -D a x/y/
in_case -D -t x/y a
in_case -D -v -t x/y/z a a2
in_case -t nosuch a
in_case -t b a
in_case -t a2
in_case dd c
in_case dd dd
in_case a a
in_case a same
in_case -m 644 a c
in_case -m 755 a c
in_case -m 444 a c
in_case -m 400 a c
in_case -m 0 a c
in_case -m u+x a c
in_case -m u=rw,g=r a c
in_case -m g+w a c
in_case -m a=rwx a c
in_case -m 555 a c
in_case -m 600 a b
in_case -m 999 a c
in_case -m xyz a c
in_case -m '' a c
in_case --mode=444 a c
in_case -m 444 a p.exe
in_case -m 444 -v a c
in_case -d x
in_case -d -v x
in_case -d x/y/z
in_case -d -v x/y/z
in_case -d x y
in_case -d dd
in_case -d -v dd/n
in_case -d a
in_case -d -m 999 x
in_case -d
in_case -d a/b
in_case -b a b
in_case -b -v a b
in_case --backup a b
in_case --backup=none a b
in_case --backup=off -v a b
in_case --backup=simple -v a b
in_case --backup=never -v a b
in_case --backup=numbered -v a b
in_case --backup=t -v a b
in_case --backup=existing -v a b
in_case --backup=nil -v a b
in_case -b -S .orig -v a b
in_case --backup=simple -S .orig -v a b
in_case -S .orig a b
in_case -S .orig -v a b
in_case --suffix=.orig -b -v a b
in_case --backup=bogus a b
in_case --backup=n a b
in_case --backup=s a b
in_case --backup=e a b
in_case --backup= a b
in_case -b a c
in_case -b -v a c
in_case -b -t dd a
in_case --backup=numbered -t dd a a2
in_case -C a c
in_case -C -v a c
in_case -C -v -m 644 a b
in_case -C -v a same
in_case -C -v -m 644 a same
in_case -C -v p.exe p.exe
in_case -C -m 755 -v a p.exe
in_case -c a c
in_case -c -v a c
in_case -p a c
in_case -p -v a c
in_case -Z a c
in_case --preserve-context a c
in_case -o nosuchuser_zz a c
in_case -g nosuchgroup_zz a c
in_case -v a a2 b dd
in_case a2 a dd
in_case -v -t dd a nosuch a2
in_case -vv a c
in_case -v -- a c
add_case status C - install --help
add_case status C - install --version
add_case status C - install --bogus a c
add_case status C - install -x a c
