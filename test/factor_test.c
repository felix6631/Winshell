/*
 * Tests of factor, run as the program it is: small numbers, primes, powers, large semiprimes up to 2^64 - 1,
 * numbers from standard input and the errors. GNU factor is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("factor", arguments, input, expected, status);
}

int main(void) {
    tools_init("factor_test");

    printf("small numbers\n");
    expect("1", "", "1:\n", 0);
    expect("0", "", "0:\n", 0);
    expect("2", "", "2: 2\n", 0);
    expect("12", "", "12: 2 2 3\n", 0);
    expect("97", "", "97: 97\n", 0);
    expect("1000", "", "1000: 2 2 2 5 5 5\n", 0);
    expect("4096", "", "4096: 2 2 2 2 2 2 2 2 2 2 2 2\n", 0);
    expect("+7", "", "7: 7\n", 0);
    expect("007", "", "7: 7\n", 0);

    printf("large numbers\n");
    expect("600851475143", "", "600851475143: 71 839 1471 6857\n", 0);
    expect("1000000007", "", "1000000007: 1000000007\n", 0);
    expect("4294967297", "", "4294967297: 641 6700417\n", 0);                    // 2^32 + 1
    expect("18446744073709551615", "", "18446744073709551615: 3 5 17 257 641 65537 6700417\n", 0);   // 2^64 - 1
    expect("18446744073709551557", "", "18446744073709551557: 18446744073709551557\n", 0);             // the largest prime below 2^64
    expect("9223372036854775807", "", "9223372036854775807: 7 7 73 127 337 92737 649657\n", 0);       // 2^63 - 1
    expect("4611686014132420609", "", "4611686014132420609: 2147483647 2147483647\n", 0);              // the square of a prime

    printf("several numbers\n");
    expect("12 15 21", "", "12: 2 2 3\n15: 3 5\n21: 3 7\n", 0);
    expect("", "12 15\n21\n\t7  8\n", "12: 2 2 3\n15: 3 5\n21: 3 7\n7: 7\n8: 2 2 2\n", 0);
    expect("", "", "", 0);

    printf("errors\n");
    expect("abc", "", "", 1);
    expect("12 abc 15", "", "12: 2 2 3\n15: 3 5\n", 1);          // the others are still done
    expect("-3", "", "", 1);
    expect("1.5", "", "", 1);
    expect("18446744073709551616", "", "", 1);                    // too large
    expect("", "12 abc 15\n", "12: 2 2 3\n15: 3 5\n", 1);
    expect("--version", "", "factor (Winshell) 0.1\n", 0);

    return test_summary("factor_test");
}
