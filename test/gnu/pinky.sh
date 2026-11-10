# GNU comparison of pinky: without sessions (the headings of the short format in all its variants), the long format of a
# user that does not exist, and the errors.
export WINSHELL_SESSIONS="$WORK/who_sessions"
mkdir -p "$WINSHELL_SESSIONS"
for o in "" -s -f -w -i -q "-f -w" "-f -i" "-w -i" "-i -q" "-s -w" nobody "-f nobody"; do
    compare pinky $o
done
compare pinky -l nosuchuser_zz
compare pinky -l -b nosuchuser_zz
compare pinky -l nosuchuser_zz other_zz
compare pinky -l
compare pinky --bogus
compare pinky -x
