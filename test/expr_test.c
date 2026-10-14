/*
 * Tests of expr, run as the program it is. The comparison with GNU expr (gnu_compare.sh) covers
 * the grammar in bulk; these are the results and exit statuses written out, and the numbers of any size.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* output, int status) {
    expect_run("expr", arguments, "", output, status);
}

int main(void) {
    tools_init("expr_test");

    printf("arithmetic\n");
    expect("1 + 2", "3\n", 0);
    expect("7 - 10", "-3\n", 0);
    expect("6 * 7", "42\n", 0);
    expect("-7 / 2", "-3\n", 0);
    expect("-7 % 3", "-1\n", 0);
    expect("2 * ( 3 + 4 )", "14\n", 0);
    expect("1 + 2 * 3", "7\n", 0);
    expect("007 + 1", "8\n", 0);

    // numbers of any size
    expect("99999999999999999999 + 1", "100000000000000000000\n", 0);
    expect("123456789012345678901234567890 * 98765432109876543210", "12193263113702179522496570642237463801111263526900\n", 0);
    expect("100000000000000000000 / 7", "14285714285714285714\n", 0);
    expect("100000000000000000000 % 7", "2\n", 0);
    expect("-100000000000000000000 / 3", "-33333333333333333333\n", 0);
    expect("1000000000 - 1", "999999999\n", 0);

    printf("comparison and logic\n");
    expect("2 < 10", "1\n", 0);
    expect("2 < 10a", "0\n", 1);       // not both numbers: by characters
    expect("007 = 7", "1\n", 0);
    expect("abc = abc", "1\n", 0);
    expect("1 | 2", "1\n", 0);
    expect("0 | 2", "2\n", 0);
    expect("\"\" | \"\"", "0\n", 1);
    expect("1 & 2", "1\n", 0);
    expect("0 & 2", "0\n", 1);
    expect("1 | 1 / 0", "1\n", 0);     // the right side is not evaluated
    expect("0 & 1 / 0", "0\n", 1);

    printf("strings\n");
    expect("abc : a", "1\n", 0);
    expect("abc : b", "0\n", 1);
    expect("abc : \"a\\(.\\)\"", "b\n", 0);
    expect("abc : \"x\\(.\\)\"", "\n", 1);
    expect("match abc \".*\"", "3\n", 0);
    expect("substr hello 2 3", "ell\n", 0);
    expect("substr hello 0 3", "\n", 1);
    expect("index hello l", "3\n", 0);
    expect("index hello xyz", "0\n", 1);
    expect("length hello", "5\n", 0);
    expect("+ length", "length\n", 0);
    expect("+ 1 + 2", "3\n", 0);

    // characters, not bytes
    expect("length 가나다", "3\n", 0);
    expect("substr 가나다 2 1", "나\n", 0);
    expect("index 가나다 다", "3\n", 0);
    expect("가나다 : \"\\(.\\)\"", "가\n", 0);

    printf("errors\n");
    expect("1 / 0", "", 2);
    expect("a + 1", "", 2);
    expect("1 +", "", 2);
    expect("( 1", "", 2);
    expect("1 2", "", 2);
    expect("abc : \"\\(a\"", "", 2);
    expect("", "", 2);

    return test_summary("expr_test");
}
