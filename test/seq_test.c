/*
 * Tests of seq, run as the program it is: whole numbers, fractions and their decimals, -w, -s, -f, negative
 * numbers as operands and the errors. GNU seq is compared with by gnu_compare.sh (section seq).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* expected, int status) {
    expect_run("seq", arguments, "", expected, status);
}

int main(void) {
    tools_init("seq_test");

    printf("whole numbers\n");
    expect("3", "1\n2\n3\n", 0);
    expect("2 4", "2\n3\n4\n", 0);
    expect("1 2 7", "1\n3\n5\n7\n", 0);
    expect("5 -2 0", "5\n3\n1\n", 0);
    expect("0", "", 0);
    expect("3 1", "", 0);
    expect("-2 2", "-2\n-1\n0\n1\n2\n", 0);
    expect("-3 -1", "-3\n-2\n-1\n", 0);
    expect("0x3", "1\n2\n3\n", 0);
    expect("9223372036854775806 9223372036854775807", "9223372036854775806\n9223372036854775807\n", 0);

    printf("fractions\n");
    expect("0.5 2", "0.5\n1.5\n", 0);
    expect("1 0.5 3", "1.0\n1.5\n2.0\n2.5\n3.0\n", 0);
    expect("0.1 0.1 0.4", "0.1\n0.2\n0.3\n0.4\n", 0);
    expect("2 -0.5 1", "2.0\n1.5\n1.0\n", 0);
    expect("1 0.25 2", "1.00\n1.25\n1.50\n1.75\n2.00\n", 0);

    printf("options\n");
    expect("-w 8 11", "08\n09\n10\n11\n", 0);
    expect("-w -2 1", "-2\n-1\n00\n01\n", 0);
    expect("-w 0.5 1.5 3", "0.5\n2.0\n", 0);
    expect("-s, 3", "1,2,3\n", 0);
    expect("--separator= 3", "123\n", 0);
    expect("--separator=ab 3", "1ab2ab3\n", 0);
    expect("-s, -w 8 10", "08,09,10\n", 0);
    expect("-f %g 3", "1\n2\n3\n", 0);
    expect("-f %.2f 1 2", "1.00\n2.00\n", 0);
    expect("-f x%gy 1 2", "x1y\nx2y\n", 0);
    expect("-f %5.1f 1 0.5 2", "  1.0\n  1.5\n  2.0\n", 0);
    expect("--format=%%%g 1 2", "%1\n%2\n", 0);
    expect("-- -1 1", "-1\n0\n1\n", 0);

    printf("errors\n");
    expect("", "", 1);
    expect("1 2 3 4", "", 1);
    expect("a", "", 1);
    expect("1 a", "", 1);
    expect("1 0 3", "", 1);
    expect("-f x 3", "", 1);
    expect("-f %g%g 3", "", 1);
    expect("-f %d 3", "", 1);
    expect("-f %g -w 3", "", 1);
    expect("-x 3", "", 1);
    expect("--bogus 3", "", 1);
    expect("nan", "", 1);
    expect("--version", "seq (Winshell) 0.1\n", 0);

    return test_summary("seq_test");
}
