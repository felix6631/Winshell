# GNU comparison of stty: without a terminal on either side, so the checks of the arguments, the messages and the statuses are
# compared (showing and setting the modes of a console is tried by hand).
printf 'x' > stty_plain.txt
for a in "" -a --all -g --save size echo -echo sane raw -raw cooked cbreak isig -isig icanon -icanon "rows 10" "cols x" "columns 80" \
         9600 38400 "intr ^C" "min 1" "time 0" speed ispeed "ispeed 9600" "ospeed 9600" ek evenp -evenp parity oddp nl -nl tabs -tabs \
         "echo -echo" "-a echo" "-g size" "-a -g" "-g -a" "-ag" foo "echo foo" --bogus -x -F rows "rows" intr "echo intr" size\ extra \
         -sane -size -speed -ek "-F stty_plain.txt" "-F stty_plain.txt size" "-F stty_plain.txt -a" "--file=stty_plain.txt -g" \
         "-F stty_plain.txt foo" "-F nosuchfile_zz" "-F nosuchfile_zz foo" "--file=nosuchfile_zz -a" "-F stty_plain.txt echo" \
         "line 0" "-parenb" "parenb" "cs8" "-cs8" "ixon -ixon" "ignbrk" "-ignbrk" "echoe" "-echoe" "opost" "-onlcr" "xcase" "-extproc"; do
    compare stty $a
done
add_case status C - stty --help
add_case status C - stty --version
