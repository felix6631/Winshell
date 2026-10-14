# GNU comparison of expr: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
compare_expr() { add_case out U - expr "$@"; }

for e in "1 + 2" "5 - 8" "6 \* 7" "7 / 2" "-7 / 2" "7 % 3" "-7 % 3" "7 % -3" "0 - 0" "007 + 1" "-0" "- 3 + 1" "1 / 0" "1 % 0" "a + 1" "1 +" \
         "99999999999999999999 + 1" "123456789012345678901234567890 \* 98765432109876543210" "100000000000000000000 / 7" \
         "100000000000000000000 % 7" "-100000000000000000000 / 3" "-100000000000000000000 % 3" "1000000000 \* 1000000000" \
         "999999999 + 1" "1000000000 - 1" "18446744073709551616 / 18446744073709551616" "5 / 100000000000000000000" \
         "1 \< 2" "2 \< 1" "10 \< 9" "a \< b" "10 \< 9a" "abc = abc" "007 = 7" "1 != 2" "1 == 1" "2 \>= 2" "3 \<= 2" "b \> a" \
         "1 \| 2" "0 \| 2" "'' \| 3" "'' \| ''" "0 \| 0" "a \| b" "1 \& 2" "0 \& 2" "1 \& 0" "'' \& 1" "a \& b" "1 \| 1 / 0" "0 \& 1 / 0" \
         "1 + 2 \* 3" "\( 1 + 2 \) \* 3" "2 \* \( 3 + 4 \) - 5" "\( 1" "1 \)" "\( \)" "1 2" "1 + 2 3" \
         "abc : abc" "abc : a" "abc : b" "abc : '.*'" "abc : 'a\(.\)'" "abc : '\(.\)\(.\)'" "abc : 'x\(.\)'" "abc : 'x'" "abc : ''" \
         "abc : 'a\|b'" "aaa : 'a*'" "abc : '[a-c]*'" "abc : '\(a\|x\)\(b\)'" "a.c : 'a\.c'" "abc : '^a'" "abc : 'c$'" "abc : 'abc$'" \
         "abc : '\(a'" "abc : 'a\)'" "abc : '\1'" "abc : 'a\{2\}'" "aab : 'a\{2\}b'" "x : '*'" "a+b : 'a+b'" "ab : 'a\+b'" \
         "match abc 'a.'" "match abc 'b'" "substr hello 2 3" "substr hello 0 3" "substr hello 1 0" "substr hello 5 10" "substr hello 6 1" \
         "substr hello -1 3" "substr hello 2 -1" "substr hello a 1" "index hello l" "index hello xyz" "index hello ol" "length hello" \
         "length ''" "length 'a b'" "+ match" "+ length abc" "+ + 1" "+ 1 + 2" "length + 1" "match" "substr hello 1" "length" \
         "1 = 1 = 1" "3 \> 2 \> 1" "2 + 3 : '.*'" "'a b' : 'a b'" "length 1234567" "- 5" "-5 + 10" "-- 5" "5 -" ; do
    eval "set -- $e"
    compare_expr "$@"
done

# UTF-8: characters
for e in "length 가나다" "substr 가나다 2 1" "index 가나다 다" "가나다 : '.*'" "가나다 : '\(.\)'" "가나다 : '가\(.\)'" "héllo : 'h.l'" ; do
    eval "set -- $e"
    compare_expr "$@"
done

add_case status C - expr
add_case status C - expr --help
