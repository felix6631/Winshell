/*
 * Tests of numfmt, run as the program it is: scaling to SI / IEC suffixes and back, the rounding styles, padding,
 * --format, --suffix, fields and delimiters, --header, --invalid and the messages. GNU numfmt is compared with by
 * gnu_compare.sh.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("numfmt", arguments, input, expected, status);
}

static void message(const char* arguments, const char* input, const char* part, int status) {
    RunResult r;

    run_tool(workDir, "numfmt", arguments, input, &r);
    CHECK_INT(r.exitCode, status);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

int main(void) {
    tools_init("numfmt_test");

    printf("--to\n");
    expect("--to=si 999", "", "999\n", 0);
    expect("--to=si 1000", "", "1.0K\n", 0);
    expect("--to=si 1001", "", "1.1K\n", 0);              // rounded up
    expect("--to=si 9999", "", "10K\n", 0);
    expect("--to=si 999999", "", "1.0M\n", 0);
    expect("--to=si 123456789", "", "124M\n", 0);
    expect("--to=iec 1024", "", "1.0K\n", 0);
    expect("--to=iec 1000000", "", "977K\n", 0);
    expect("--to=iec-i 1536", "", "1.5Ki\n", 0);
    expect("--to=si 1.05", "", "1\n", 0);
    expect("--to=si 2.5", "", "2\n", 0);                  // below 10 without a suffix: printed as printf does
    expect("--to=si 10.4", "", "11\n", 0);
    expect("--to=si 1000000000000000000000000", "", "1.0Y\n", 0);

    printf("rounding\n");
    expect("--round=down --to=si 1999", "", "1.9K\n", 0);
    expect("--round=nearest --to=si 1050", "", "1.1K\n", 0);
    expect("--round=nearest --to=si 1049", "", "1.0K\n", 0);
    expect("--round=towards-zero --to=si 1999", "", "1.9K\n", 0);
    expect("--round=up --to=si -- -1001", "", "-1.0K\n", 0);
    expect("--round=from-zero --to=si -- -1001", "", "-1.1K\n", 0);
    expect("--to-unit=2 5", "", "3\n", 0);
    expect("--round=down --to-unit=2 5", "", "2\n", 0);

    printf("--from\n");
    expect("--from=si 1.5K", "", "1500\n", 0);
    expect("--from=iec 1K", "", "1024\n", 0);
    expect("--from=auto 1Ki", "", "1024\n", 0);
    expect("--from=iec-i 2Mi", "", "2097152\n", 0);
    expect("--from=si --to=iec-i 1M", "", "977Ki\n", 0);
    expect("--from-unit=2 10", "", "20\n", 0);
    expect("--from-unit=1Ki 2", "", "2048\n", 0);
    expect("5.50", "", "5.50\n", 0);                       // the precision of the input stays
    message("1K", "", "rejecting suffix in input: '1K' (consider using --from)", 2);
    message("--from=si 1Ki", "", "invalid suffix in input '1Ki': 'i'", 2);
    message("--from=iec-i 1K", "", "missing 'i' suffix", 2);
    message("--from=si abc", "", "invalid number: 'abc'", 2);
    message("--from=si 1e3", "", "invalid suffix in input: '1e3'", 2);
    message("--from-unit=0 1", "", "invalid unit size: '0'", 1);

    printf("padding, format, suffix\n");
    expect("--padding=8 5", "", "       5\n", 0);
    expect("--padding=-8 5", "", "5       \n", 0);
    expect("--format=%05f 5", "", "00005\n", 0);
    expect("--format=%.2f 5", "", "5.00\n", 0);
    expect("--format=[%6.1f] 12.34", "", "[  12.4]\n", 0);
    expect("--to=si --format=%.3f 1500", "", "1.500K\n", 0);
    expect("--suffix=B 5", "", "5B\n", 0);
    expect("--suffix=B --to=si 1500", "", "1.5KB\n", 0);
    expect("--suffix=B --from=si 1.5KB", "", "1500B\n", 0);
    message("--format=%d 5", "", "directive must be", 1);
    message("--format=abc 5", "", "has no % directive", 1);
    message("--format=%f%f 5", "", "too many % directives", 1);
    message("--padding=0 5", "", "invalid padding value '0'", 1);

    printf("lines, fields, delimiters\n");
    expect("--to=si", "1000 2000 3000\n", "1.0K 2000 3000\n", 0);
    expect("--to=si --field=2", "1000 2000 3000\n", "1000 2.0K 3000\n", 0);
    expect("--to=si --field=1-", "1000 2000 3000\n", "1.0K 2.0K 3.0K\n", 0);
    expect("--to=si --field=-2", "1000 2000 3000\n", "1.0K 2.0K 3000\n", 0);
    expect("--to=si --field=1,3", "1000 2000 3000\n", "1.0K 2000 3.0K\n", 0);
    expect("--to=si --field=2", "  1000   2000\n", "  1000   2.0K\n", 0);   // the blanks keep the width
    expect("--to=si", "  1000   2000\n", "  1.0K   2000\n", 0);
    expect("--to=si -d, --field=2", "1000,2000,3000\n", "1000,2.0K,3000\n", 0);
    expect("--to=si 1000 2000", "", "1.0K\n2.0K\n", 0);                    // the operands are lines
    expect("--to=si", "1000\r\n2000\r\n", "1.0K\n2.0K\n", 0);               // CR LF ends a line as LF does
    expect("--to=si --header", "size\n1000\n", "size\n1.0K\n", 0);
    expect("--to=si --header=2", "a\nb\n1000\n", "a\nb\n1.0K\n", 0);
    expect("--to=si", "1000\n2000", "1.0K\n2.0K", 0);                       // no end of line added
    expect("--to=si -z", "1000\0002000\000", "1.0K\0002.0K\000", 0);
    message("--field=0", "", "fields are numbered from 1", 1);
    message("--field=2-1", "", "invalid decreasing range", 1);
    message("-d ab", "", "the delimiter must be a single character", 1);
    message("--header=0", "", "invalid header value '0'", 1);

    printf("--invalid\n");
    expect("--to=si --invalid=ignore", "a\n1000\n", "a\n1.0K\n", 0);
    message("--to=si --invalid=warn", "a\n1000\n", "invalid number: 'a'", 0);
    message("--to=si --invalid=fail", "a\n1000\n", "invalid number: 'a'", 2);
    expect("--to=si --invalid=fail", "a\n1000\n", "a\n1.0K\n", 2);
    message("--to=si", "a\n1000\n", "invalid number: 'a'", 2);               // abort: stops at the first
    message("--invalid=bogus", "", "invalid argument 'bogus' for '--invalid'", 1);
    message("--to=bogus", "", "Valid arguments are:", 1);
    message("--grouping --to=si", "", "grouping cannot be combined with --to", 1);

    printf("limits\n");
    message("99999999999999999999", "", "value too large to be printed", 2);
    message("123456789012345678901234567890", "", "value too large to be converted", 2);
    message("--to=si 1000000000000000000000000000", "", "value too large to be converted", 2);
    expect_run("numfmt", "--version", "", "numfmt (Winshell) 0.1\n", 0);
    return test_summary("numfmt_test");
}
