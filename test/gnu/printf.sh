# GNU comparison of printf: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# Only stdout and the exit code are compared; the texts of the errors go to stderr.

# plain text and escapes in the format
for f in 'hello' 'hello\n' 'a\tb' '\a\b\e\f\r\v' '\"q\"' '\101\102' '\0101' '\x41\x4a' '\x4' 'a\cb' '100%%' '%%%%' 'no newline'; do
    compare printf "$f"
    compare printf "$f" x y
done
# the Unicode escapes and character constants are UTF-8 here: compared with the GNU tools in a UTF-8 locale
add_case out U - printf 'é€\n'
add_case out U - printf '\U0001F600\n'
add_case out U - printf '%x\n' "'é"
add_case out U - printf '%d\n' "'가"
# (a double backslash in an argument is changed by the batch runner's quoting: run at once)
compare_now printf 'a\\b\n'
compare_now printf '%b\n' 'a\nb\\c'
compare printf 'trailing backslash\'
compare printf '\q\z'
compare printf '\x'
compare printf '\u12'
compare printf 'A'
compare printf '\uD800'

# strings
compare printf '%s\n' hello
compare printf '%s\n' hello world
compare printf '[%s]\n' ''
compare printf '[%10s]\n' abc
compare printf '[%-10s]\n' abc
compare printf '[%.2s]\n' abcdef
compare printf '[%10.2s]\n' abcdef
compare printf '[%-10.2s]\n' abcdef
compare printf '[%*s]\n' 6 abc
compare printf '[%-*s]\n' 6 abc
compare printf '[%*s]\n' -6 abc
compare printf '[%.*s]\n' 2 abcdef
compare printf '[%*.*s]\n' 8 3 abcdef
compare printf '%s %s\n' a
compare printf '%s\n'
compare printf '%s-%s\n' a b c d e
compare printf '%s\n' '가나다'
compare printf '[%5s]\n' '가'

# characters
compare printf '%c\n' hello
compare printf '[%c]\n' ''
compare printf '[%5c]\n' x
compare printf '[%-5c]\n' x
compare printf '%c%c%c\n' a b c

# %b
compare printf '%b\n' 'a\tb'
compare printf '%b\n' '\0101'
compare printf '%b\n' '\101'
compare printf '%b\n' '\x41'
compare printf '%b|%b\n' 'x\cy' z
compare printf '%b\n' 'é'
compare printf '%b\n' '100%'
compare printf '%s\n' 'a\tb'

# integers
for v in 0 1 -1 42 255 -255 0x1f 0X1F 010 -010 99999999999 -99999999999 9223372036854775807 -9223372036854775808 9223372036854775808 18446744073709551615 18446744073709551616 +5 ' 7' '7 ' abc 12abc 0x '' - + ; do
    compare printf '%d\n' "$v"
    compare printf '%i|%o|%u|%x|%X\n' "$v" "$v" "$v" "$v" "$v"
done
compare printf '[%5d]\n' 42
compare printf '[%-5d]\n' 42
compare printf '[%05d]\n' 42
compare printf '[%+d]\n' 42
compare printf '[% d]\n' 42
compare printf '[%+5d]\n' -42
compare printf '[%.5d]\n' 42
compare printf '[%8.5d]\n' 42
compare printf '[%-8.5d]\n' -42
compare printf '[%#o]\n' 8
compare printf '[%#x]\n' 255
compare printf '[%#X]\n' 255
compare printf '[%#x]\n' 0
compare printf '[%08x]\n' 255
compare printf '[%*d]\n' 6 42
compare printf '[%-*d]\n' 6 42
compare printf '[%.*d]\n' 4 42
compare printf '[%ld|%lld|%hd|%jd|%zd]\n' 1 2 3 4 5
compare printf '%d %d\n' 1 2 3 4 5
compare printf '%d\n'
compare printf '%x\n' -1
compare printf '%u\n' -1
compare printf '%o\n' -8

# character constants as numbers
compare printf '%d\n' "'a"
compare printf '%d\n' '"A'
compare printf '%d\n' "'ab"
compare printf '%d\n' "'"

# floating point
for v in 0 1 -1 3.14159 -2.5 1e3 1e-3 0x1p4 0x1.8p1 .5 5. inf -inf nan abc 1.5x '' 100000000 0.000001 123456789.123456789; do
    compare printf '%f\n' "$v"
    compare printf '%e|%E\n' "$v" "$v"
    compare printf '%g|%G\n' "$v" "$v"
done
compare printf '[%10.3f]\n' 3.14159
compare printf '[%-10.3f]\n' 3.14159
compare printf '[%010.3f]\n' 3.14159
compare printf '[%+.2f]\n' 3.14159
compare printf '[% .2f]\n' 3.14159
compare printf '[%.0f]\n' 2.5
compare printf '[%.0f]\n' 3.5
compare printf '[%#.0f]\n' 3
compare printf '[%*.*f]\n' 9 2 3.14159
compare printf '[%.3g]\n' 1234.5678
compare printf '[%.10g]\n' 1234.5678
compare printf '[%#g]\n' 1
compare printf '[%a]\n' 1
compare printf '[%A]\n' 10.5
compare printf '[%.2a]\n' 1.999
compare printf '[%f]\n' 1
compare printf '%f\n' "'a"
compare printf '%5.1f%%\n' 99.44

# reuse of the format
compare printf '%s\n' a b c
compare printf '%s=%s\n' a 1 b 2 c
compare printf '%d:%s\n' 1 a 2 b
compare printf 'x\n' a b c
compare printf '%s %s %s\n' 1 2 3 4 5 6 7
compare printf '%5s|%-5s|\n' a b c d

# errors
compare printf
compare printf --
compare printf -- '%s\n' a
compare printf '%z\n' a
compare printf '%'
compare printf '%5'
compare printf '%y' 1
compare printf '%5%' 1
compare printf '%.3q' 1
add_case status C - printf --help
add_case status C - printf --version
compare printf '%s\n' --help
compare printf -v x '%s' a
compare printf -n
add_case status C - printf '%d\n' abc
add_case status C - printf '%d\n' 12abc
add_case status C - printf '%s\n' ok
add_case status C - printf '%f\n' x
