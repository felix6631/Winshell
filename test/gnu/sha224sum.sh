# GNU comparison of sha224sum: the cases are in _sums.sh.
[ "$(type -t sum_cases)" = function ] || . "$HERE/gnu/_sums.sh"
sum_cases sha224sum
