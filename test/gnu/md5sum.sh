# GNU comparison of md5sum: the cases are in _sums.sh.
[ "$(type -t sum_cases)" = function ] || . "$HERE/gnu/_sums.sh"
sum_cases md5sum
