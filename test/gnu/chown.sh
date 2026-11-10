# GNU comparison of chown: the owners here are not those of MSYS, so what is compared is the user and the messages that
# do not depend on the numbers: the names that do not exist, the files that do not exist, the operands and the options.
own_run() {
    local who=$1 tool=$2 rc
    shift 2
    rm -rf own_dir
    mkdir own_dir
    : > own_dir/f
    mkdir own_dir/d
    ( cd own_dir && MSYS2_ARG_CONV_EXCL="*" "$who" $tool "$@" > ../own_out.txt 2> ../own_err.txt ); rc=$?
    cat own_out.txt
    echo '-- err'; tr -d '\r' < own_err.txt | sed -e "s#^[^ :]*$tool:#$tool:#" -e "s#^Try '[^ ]*$tool #Try '$tool #"
    echo "-- rc $rc"
}
own_case() {
    local tool=$1 g o
    shift
    total=$((total + 1))
    g=$(own_run gnu $tool "$@")
    o=$(own_run ours $tool "$@")
    if [ "$g" != "$o" ]; then
        bad=$((bad + 1))
        echo "DIFFERS: $tool $*"
    fi
}
me=$(id -un)
own_case chown $me f
own_case chown -v $me f
own_case chown -c $me f
own_case chown -f $me f
own_case chown -v : f
own_case chown -v '' f
own_case chown -v $me f d
own_case chown -R -v $me d
own_case chown -R $me d f
own_case chown -vh $me f
own_case chown -v --dereference $me f
own_case chown -v nosuchuser_zz f
own_case chown -v :nosuchgroup_zz f
own_case chown -v $me:nosuchgroup_zz f
own_case chown -v nosuchuser_zz:nosuchgroup_zz f
own_case chown -v root f
own_case chown -v $me nosuchfile
own_case chown -f $me nosuchfile
own_case chown -c $me nosuchfile
own_case chown -v $me f nosuchfile d
own_case chown -v --from=nosuchuser_zz $me f
own_case chown -v --from=$me $me f
own_case chown -v --from=:nosuchgroup_zz $me f
own_case chown
own_case chown $me
own_case chown -v
own_case chown --reference=f
own_case chown --reference=nosuchfile f
own_case chown --preserve-root -R $me /
own_case chown -H -R $me d
own_case chown -L -R $me d
own_case chown -P -R $me d
add_case status C - chown --help
add_case status C - chown --version
add_case status C - chown --bogus x f
add_case status C - chown -x x f
