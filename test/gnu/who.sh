# GNU comparison of who: without sessions on either side (the MSYS who has no utmp file; ours reads an empty directory), so
# the headings, the counts, the errors and the exit codes are compared. The boot line (-b, -a) exists only here.
export WINSHELL_SESSIONS="$WORK/who_sessions"
mkdir -p "$WINSHELL_SESSIONS"
for o in "" -H -s -u "-u -H" -T "-T -H" -q "-q -H" -m "-m -H" -d -l -p -r -t "-d -H" "-l -H" "-p -H" "-r -H" "-t -H" -w "-s -H" "-u -T -H"; do
    compare who $o
done
compare who am i
compare who -H am i
compare who mom likes
compare who nosuchfile
compare who a b c
compare who --bogus
compare who -x
