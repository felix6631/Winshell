/*
 * Tests of printf, run as the program it is: escapes, conversions, reuse of the format, the numeric
 * errors and the UTF-8 forms. GNU printf is compared with by gnu_compare.sh (section printf).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* expected, int status) {
    expect_run("printf", arguments, "", expected, status);
}

int main(void) {
    tools_init("printf_test");

    printf("text and escapes\n");
    expect("hello", "hello", 0);
    expect("\"a\\tb\\n\"", "a\tb\n", 0);
    expect("\"\\101\\x42\\u00e9\"", "AB\xc3\xa9", 0);
    expect("\"a\\cb\"", "a", 0);
    expect("\"100%%\"", "100%", 0);
    expect("\"\\x\"", "", 1);

    printf("conversions\n");
    expect("\"%s|%5s|%-5s|%.2s\" abc abc abc abc", "abc|  abc|abc  |ab", 0);
    expect("\"%d|%5d|%-5d|%05d|%+d\" 42 42 42 42 42", "42|   42|42   |00042|+42", 0);
    expect("\"%x|%X|%#x|%o|%u\" 255 255 255 8 7", "ff|FF|0xff|10|7", 0);
    expect("\"%d,\" 0x10 010 -5", "16,8,-5,", 0);
    expect("\"%u\" -1", "18446744073709551615", 0);
    expect("\"%.2f|%8.3f|%e\" 3.14159 2.5 1500", "3.14|   2.500|1.500000e+03", 0);
    expect("\"%c%c\" hello world", "hw", 0);
    expect("\"%b\" \"a\\tb\\0101\"", "a\tbA", 0);
    expect("\"[%*d][%-*d][%.*f]\" 4 7 4 7 1 2.55", "[   7][7   ][2.5]", 0);
    expect("\"%a|%a|%A|%.1a\" 1 0.5 10.5 1.96875", "0x1p+0|0x1p-1|0X1.5P+3|0x2.0p+0", 0);

    printf("character constants\n");
    expect("\"%d\" \"'a\"", "97", 0);
    expect("\"%d %d\" \"'\xc3\xa9\" \"'\xea\xb0\x80\"", "233 44032", 0);

    printf("reusing the format\n");
    expect("\"%s\\n\" a b c", "a\nb\nc\n", 0);
    expect("\"%s=%s;\" a 1 b", "a=1;b=;", 0);
    expect("\"%s %d\\n\"", " 0\n", 0);
    expect("\"x\" a b", "x", 0);

    printf("errors\n");
    expect("\"%d\" abc", "0", 1);
    expect("\"%d\" 12abc", "12", 1);
    expect("\"%q\" a", "", 1);
    expect("", "", 1);
    expect("\"%\"", "", 1);

    return test_summary("printf_test");
}
