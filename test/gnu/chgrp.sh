# GNU comparison of chgrp: the owners here are not those of MSYS, so what is compared is the user and the messages that
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
own_case chgrp -v nosuchuser_zz f
own_case chgrp -v :nosuchgroup_zz f
own_case chgrp -v nosuchuser_zz:nosuchgroup_zz f
own_case chgrp -v root f
own_case chgrp
own_case chgrp -v
own_case chgrp --reference=f
own_case chgrp --reference=nosuchfile f
add_case status C - chgrp --help
add_case status C - chgrp --version
add_case status C - chgrp --bogus x f
add_case status C - chgrp -x x f
