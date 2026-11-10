# GNU comparison of mknod: the checks of the operands and the errors that do not depend on a file being made.
printf 'x' > mn_exists.txt
compare mknod
compare mknod mn_a
compare mknod mn_a p 1 2
compare mknod mn_a p extra
compare mknod mn_a b
compare mknod mn_a q
compare mknod mn_a ab
compare mknod mn_a b 1
compare mknod mn_a c 1 2 3
compare mknod mn_a c 1 x
compare mknod mn_a c x 1
compare mknod mn_a c 99999999999999999999 1
compare mknod mn_a c 1 99999999999999999999
compare mknod mn_a q 1 2
compare mknod mn_a q x y
compare mknod -m zzz mn_a p
compare mknod -m 755
compare mknod mn_exists.txt p
compare mknod mn_exists.txt c 1 2
compare mknod mn_exists.txt u 0x1 010
compare mknod nosuchdir_zz/mn_a p
compare mknod nosuchdir_zz/mn_a c 1 2
compare mknod --bogus
add_case status C - mknod --help
add_case status C - mknod --version
