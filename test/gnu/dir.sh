# GNU comparison of dir (and of ls -b): the names in columns, the blanks and backslashes written as escapes. The names are
# lower case only, because ls sorts without regard to case (the Windows way), and only the options that list the same on both.
mkdir -p dr_dir/sub dr_dir/other
printf 'x' > "dr_dir/a b"
printf 'x' > dr_dir/c.txt
printf 'x' > dr_dir/d.txt
printf 'x' > dr_dir/sub/inner.txt
printf 'x' > dr_dir/sub/in\ ner.txt
printf 'x' > dr_dir/.hid
printf 'x' > dr_dir/z.dat
for o in "" -1 -C -a -A -F -p -r -d -b -1b -R "-R -F" "-a -r" "-1 -a -F"; do
    compare dir $o dr_dir
    compare dir $o dr_dir/sub
    compare dir $o dr_dir/c.txt dr_dir/sub
done
compare dir dr_dir/a\ b
compare dir -d dr_dir dr_dir/sub
compare dir dr_dir/nosuchfile_zz
compare ls -b dr_dir
compare ls -b -1 dr_dir/sub
compare ls -1 dr_dir
