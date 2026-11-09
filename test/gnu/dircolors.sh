# GNU comparison of dircolors: each case writes a database file and compares the output with the TERM and SHELL of the case.
printf 'TERM xterm*\nDIR 01;35\n.txt 00;32\n*.md 01;33 # comment\nFILE 4\nEXEC 1;2\nLINK target\nOWT 1\nSETUID 7\nNORMAL 0\nRESET 2\nBLK 1\nCHR 2\nFIFO 3\nSOCK 4\nDOOR 5\nSTICKY 6\nCAPABILITY 7\nOTHER_WRITABLE 1\nSTICKY_OTHER_WRITABLE 2\nSETGID 3\n' > dc_ok.txt
printf 'TERM xterm*\nDIR 01;35\nHARDLINK 8\nBOGUS 5\nTERM vt100\nREG 4\n' > dc_bad.txt
printf 'TERM x\nDIR\n' > dc_missing.txt
printf 'TERM\n' > dc_term.txt
printf '.a=b 1\n* 1\n.c 3\n' > dc_odd.txt
printf 'TERM x\nDIR 1;2\nDIR 3\n' > dc_dup.txt
printf 'TERM x\nDIR a'"'"'b\n' > dc_quote.txt
printf 'TERM x\nDIR 1:2\nLINK a=b\n' > dc_colon.txt
printf 'COLOR all\nOPTIONS -F\nEIGHTBIT 1\nTERM x\n' > dc_ignored.txt
printf 'TERM x\nDIR 01;34   # tail\n LINK   target  \n\tNORM 1\n# comment\n\n   \n' > dc_space.txt
printf 'TERM a*\nDIR 1\nTERM b*\nDIR 2\nTERM c*\nFIFO 3\n' > dc_terms.txt
printf 'TERM [a-c]x\nDIR 1\nTERM ?y\nLINK 2\nTERM z[!a]\nFILE 3\n' > dc_globs.txt
printf 'dir 1\nTerm x\nDir 2\n' > dc_case.txt
printf 'TERM x\nDIR 1\r\nLINK 2\r\n' > dc_crlf.txt
printf '' > dc_empty.txt
dc_run() {
    local who=$1 term=$2 shell=$3 rc
    shift 3
    if [ "$term" = UNSET ]; then unset TERM; else export TERM=$term; fi
    if [ "$shell" = UNSET ]; then unset SHELL; else export SHELL=$shell; fi
    MSYS2_ARG_CONV_EXCL="*" "$who" dircolors "$@" > dc_out.txt 2> dc_err.txt; rc=$?
    cat dc_out.txt
    echo '-- err'; tr -d '\r' < dc_err.txt | sed -e 's#^[^ :]*dircolors:#dircolors:#' -e "s#^Try '[^ ]*dircolors #Try 'dircolors #"
    echo "-- rc $rc"
}
dc_case() {
    local term=$1 shell=$2 g o
    shift 2
    total=$((total + 1))
    g=$(dc_run gnu "$term" "$shell" "$@")
    o=$(dc_run ours "$term" "$shell" "$@")
    if [ "$g" != "$o" ]; then
        bad=$((bad + 1))
        echo "DIFFERS: [TERM=$term SHELL=$shell] dircolors $*"
    fi
}
for term in xterm xterm-256color vt100 dumb linux UNSET ax by cz zb zz; do
    dc_case $term /bin/bash
    dc_case $term /bin/bash -b
    dc_case $term /bin/bash -c
    for f in dc_ok dc_bad dc_missing dc_term dc_odd dc_dup dc_quote dc_colon dc_ignored dc_space dc_terms dc_globs dc_case dc_empty; do
        dc_case $term /bin/bash -b $f.txt
    done
done
dc_case x /bin/bash -c dc_ok.txt
dc_case x /bin/bash -c dc_odd.txt
dc_case x /bin/bash -c dc_quote.txt
dc_case x /bin/bash dc_colon.txt
dc_case x /bin/bash dc_crlf.txt
dc_case xterm /bin/tcsh
dc_case xterm /bin/csh
dc_case xterm /usr/bin/zsh
dc_case xterm /bin/tcsh -b
dc_case xterm /bin/bash -c
dc_case xterm /bin/bash -b -c
dc_case xterm /bin/bash -c -b
dc_case xterm /bin/bash --sh
dc_case xterm /bin/bash --bourne-shell
dc_case xterm /bin/bash --csh
dc_case xterm /bin/bash --c-shell
dc_case xterm /bin/bash -p
dc_case xterm /bin/bash --print-database
dc_case xterm /bin/bash -p dc_ok.txt
dc_case xterm /bin/bash -b -p
dc_case xterm /bin/bash -p -c
dc_case xterm /bin/bash -b dc_ok.txt dc_ok.txt
dc_case xterm /bin/bash dc_ok.txt dc_bad.txt
dc_case xterm /bin/bash dc_nosuchfile.txt
dc_case xterm /bin/bash -b dc_nosuchfile.txt
add_case status C - dircolors --bogus
add_case status C - dircolors --help
add_case status C - dircolors --version
