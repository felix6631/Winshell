# GNU comparison of yes: it never ends, so only what ends it is compared (the options and the exit statuses);
# the lines it prints are checked by yes_test through head.
add_case status C - yes --help
add_case status C - yes --version
add_case status C - yes --bogus
add_case status C - yes -x
add_case status C - yes --help extra
