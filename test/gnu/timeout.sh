# GNU comparison of timeout: the output and the status of commands that end in time or time out, the checks of the options
# (not the messages of the C library for an unknown option). Signals are not compared: Windows has none, the status of a
# command that is ended is 124 (137 for KILL, which the harness of the comparison cannot tell from GNU's death by a signal).
for d in 5 5s 0.1m 1e1 .5 0 " 2" 1d 0.001h; do
    compare timeout "$d" echo hi
    compare timeout "$d" false
done
compare timeout 5 echo -n x
compare timeout -- 5 echo hi
compare timeout --foreground 5 echo hi
compare timeout -v 5 echo hi
compare timeout -s TERM 5 echo hi
compare timeout -s 9 5 echo hi
compare timeout -k 1 5 echo hi
compare timeout --kill-after=1 5 echo hi
compare timeout --signal=INT 5 echo hi
compare timeout --preserve-status 5 echo hi
compare timeout --preserve-status 5 false
for d in 0.2 0.2s 0.003m; do
    compare timeout "$d" sleep 20
    compare timeout -v "$d" sleep 20
    compare timeout -s INT "$d" sleep 20
    compare timeout -s HUP "$d" sleep 20
    compare timeout -s 15 "$d" sleep 20
    compare timeout --foreground "$d" sleep 20
done
compare timeout
compare timeout 5
compare timeout -k 1
compare timeout abc sleep 1
compare timeout 1x sleep 1
compare timeout 1m1 sleep 1
compare timeout nan sleep 1
compare timeout 1e sleep 1
compare timeout -k abc 5 sleep 1
compare timeout -s FOO 5 sleep 1
compare timeout -s 99 5 sleep 1
compare timeout -s -1 5 sleep 1
compare timeout 1 nosuchcommand_zz
compare timeout 1 /nonexistent/x
add_case status C - timeout --help
add_case status C - timeout --version
