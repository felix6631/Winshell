# GNU comparison of true (and false in false.sh): the arguments are ignored, the exit code is the result.
compare true
compare true a b c
compare true --bogus
compare true -- x
add_case status C - true --version
