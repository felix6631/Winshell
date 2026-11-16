# GNU comparison of printenv: variables that are the same on both sides (LC_ALL is set by the comparison), the status for the
# ones that are not set, -0 and the options.
compare printenv LC_ALL
compare printenv LC_ALL LC_ALL
compare printenv NOSUCH_ZZ
compare printenv LC_ALL NOSUCH_ZZ
compare printenv NOSUCH_ZZ LC_ALL
compare printenv -0 LC_ALL
compare printenv --null LC_ALL
compare printenv -0 LC_ALL LC_ALL
compare printenv 'LC_ALL=C'
compare printenv ''
compare printenv =
compare printenv LC
compare printenv -- LC_ALL
compare printenv --bogus
compare printenv -x
add_case status C - printenv
add_case status C - printenv --help
add_case status C - printenv --version
