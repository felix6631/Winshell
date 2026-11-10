# GNU comparison of mkfifo: the errors that do not depend on a FIFO being made (GNU makes one, here nothing can be made).
printf 'x' > mf_exists.txt
compare mkfifo
compare mkfifo -m
compare mkfifo -m 755
compare mkfifo -m zzz mf_a
compare mkfifo -m 9999 mf_a
compare mkfifo -Z
compare mkfifo --bogus
compare mkfifo -x
compare mkfifo mf_exists.txt
compare mkfifo nosuchdir_zz/mf_a
compare mkfifo ''
compare mkfifo mf_exists.txt nosuchdir_zz/mf_a
compare mkfifo mf_exists.txt/mf_a
add_case status C - mkfifo --help
add_case status C - mkfifo --version
