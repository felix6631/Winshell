# The comparison of the tools that change files (mkdir, rmdir, touch, cp, mv, rm): every case runs in a fresh directory,
# once with the GNU tool and once with ours, and what is left (the names, the kinds, the sizes and the contents), what the
# tool wrote and its exit status are compared. Every file of these sections sources this file; as a section of its own it
# only defines the function.
#
#   fs_case TOOL 'setup commands' ARGS...
#
# The setup runs in the directory before the tool does; the arguments are the ones of the tool (in the directory).
export MSYS2_ARG_CONV_EXCL='*'

fs_tree() {
    find . -mindepth 1 | sort | while read -r f; do
        if [ -d "$f" ]; then
            echo "$f d"
        else
            echo "$f f $(stat -c "${FS_FMT:-%s}" "$f") $(od -An -c "$f" | tr -s ' ' | head -c 80)"
        fi
    done
}

# the messages, without what is the C library of the other side: the path of the program, the options it does not know
fs_messages() {
    sed 's#/usr/bin/##g' "$1" | grep -v "^Try '" | grep -v "option"
}

fs_case() {
    local tool=$1 setup=$2 who g o grc orc
    shift 2
    total=$((total + 1))
    for who in gnu ours; do
        rm -rf fs_dir
        mkdir fs_dir
        ( cd fs_dir && eval "$setup" )
        if [ $who = gnu ]; then
            ( cd fs_dir && gnu "$tool" "$@" > ../fs_out.txt 2> ../fs_err.txt < /dev/null ); grc=$?
            g=$( cd fs_dir && fs_tree; sed 's#/usr/bin/##g' ../fs_out.txt; echo "--"; fs_messages ../fs_err.txt )
        else
            ( cd fs_dir && ours "$tool" "$@" > ../fs_out.txt 2> ../fs_err.txt < /dev/null ); orc=$?
            o=$( cd fs_dir && fs_tree; sed 's#/usr/bin/##g' ../fs_out.txt; echo "--"; fs_messages ../fs_err.txt )
        fi
    done
    if [ "$g" != "$o" ] || [ $grc != $orc ]; then
        bad=$((bad + 1))
        echo "DIFFERS: $tool $* [$setup] (status $grc, ours $orc)"
        if [ -n "$FS_VERBOSE" ]; then
            echo "--- gnu"; echo "$g"; echo "--- ours"; echo "$o"
        fi
    fi
}
