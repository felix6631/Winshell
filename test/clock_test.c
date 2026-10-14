/*
 * Tests of sleep and date, run as the programs they are. The comparison with GNU date (gnu_compare.sh)
 * covers the formats and the dates it reads; these are the waiting of sleep, the exit statuses and what
 * does not depend on the zone of the machine.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void expect(const char* tool, const char* arguments, const char* output, int status) {
    RunResult r;
    run_tool(workDir, tool, arguments, "", &r);
    CHECK_STR(r.out, output);
    CHECK_INT(r.exitCode, status);
    run_result_free(&r);
}

static void test_sleep(void) {
    RunResult r;
    ULONGLONG start;

    printf("sleep\n");

    start = GetTickCount64();
    run_tool(workDir, "sleep", "0.3", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(GetTickCount64() - start >= 250);
    run_result_free(&r);

    // the operands are added, and the suffixes are seconds, minutes, hours and days
    start = GetTickCount64();
    run_tool(workDir, "sleep", "0.1 0.1s 0.0005m", "", &r);     // 0.1 + 0.1 + 0.03
    CHECK_INT(r.exitCode, 0);
    CHECK(GetTickCount64() - start >= 200);
    run_result_free(&r);

    expect("sleep", "0", "", 0);
    expect("sleep", "", "", 1);
    expect("sleep", "abc", "", 1);
    expect("sleep", "1x", "", 1);
    expect("sleep", "0.1 oops", "", 1);
    expect("sleep", "--bogus", "", 1);
}

static void test_date(void) {
    RunResult r;

    printf("date\n");

    // fixed instants in UTC
    expect("date", "-u -d @0 +%F|%T|%Z|%s", "1970-01-01|00:00:00|UTC|0\n", 0);
    expect("date", "-u -d @1700000000 +%Y-%m-%d_%H:%M:%S", "2023-11-14_22:13:20\n", 0);
    expect("date", "-u -d @1700000000 +%a|%A|%b|%B|%j|%u|%w|%V", "Tue|Tuesday|Nov|November|318|2|2|46\n", 0);
    expect("date", "-u -d @1700000000 +%e|%-d|%_m|%k|%l|%I|%p|%P", "14|14|11|22|10|10|PM|pm\n", 0);
    expect("date", "-u -d @1700000000 +%^a|%#b|%10Y|%-H", "TUE|NOV|0000002023|22\n", 0);
    expect("date", "-u -d @1700000000 +%c", "Tue Nov 14 22:13:20 2023\n", 0);
    expect("date", "-u -d @1700000000 +%D|%x|%X|%r|%R", "11/14/23|11/14/23|22:13:20|10:13:20 PM|22:13\n", 0);
    expect("date", "-u -d @1700000000 +%z|%:z|%::z", "+0000|+00:00|+00:00:00\n", 0);
    expect("date", "-u -d @1700000000.25 +%s.%3N", "1700000000.250\n", 0);
    expect("date", "-u -d @-86400 +%F", "1969-12-31\n", 0);
    expect("date", "-u -d @253402300799 +%F|%T", "9999-12-31|23:59:59\n", 0);
    expect("date", "-u -d @0 +%%|%t|%q", "%|\t|1\n", 0);

    // years at the end of an ISO year
    expect("date", "-u -d 2021-01-03 +%G-W%V-%u", "2020-W53-7\n", 0);
    expect("date", "-u -d 2024-12-30 +%G-W%V-%u", "2025-W01-1\n", 0);

    // the forms of -d
    expect("date", "-u -d 2026-10-14T13:45:10Z +%s", "1791985510\n", 0);
    expect("date", "-u -d 2026-10-14T13:45:10+09:00 +%F_%T", "2026-10-14_04:45:10\n", 0);
    expect("date", "-u -d \"2026-10-14 +2 days\" +%F", "2026-10-16\n", 0);
    expect("date", "-u -d \"2026-10-14 3 weeks ago\" +%F", "2026-09-23\n", 0);
    expect("date", "-u -d \"2026-10-31 +1 month\" +%F", "2026-12-01\n", 0);
    expect("date", "-u -d \"2024-02-29 next year\" +%F", "2025-03-01\n", 0);
    expect("date", "-u -d \"14 Oct 2026 10:30\" +%F_%R", "2026-10-14_10:30\n", 0);
    expect("date", "-u -d \"Oct 14, 2026\" +%F", "2026-10-14\n", 0);
    expect("date", "-u -d \"2026-10-14 12:30 pm\" +%R", "12:30\n", 0);

    // the shortcuts for the whole date
    expect("date", "-u -d @1700000000 -I", "2023-11-14\n", 0);
    expect("date", "-u -d @1700000000 --iso-8601=seconds", "2023-11-14T22:13:20+00:00\n", 0);
    expect("date", "-u -d @1700000000 -R", "Tue, 14 Nov 2023 22:13:20 +0000\n", 0);
    expect("date", "-u -d @1700000000 --rfc-3339=seconds", "2023-11-14 22:13:20+00:00\n", 0);

    // the date of a file
    write_text(workDir, "dated.txt", "x");
    run_tool(workDir, "date", "-r dated.txt +%s", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strlen(r.out) > 8);
    run_result_free(&r);

    // now: something that looks like a date, and the same time twice
    run_tool(workDir, "date", "+%s", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(atoll(r.out) > 1700000000LL);
    run_result_free(&r);
    run_tool(workDir, "date", "", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strlen(r.out) > 20);
    run_result_free(&r);

    // mistakes
    expect("date", "-d nonsense +%F", "", 1);
    expect("date", "-d 2026-13-45 +%F", "", 1);
    expect("date", "extra", "", 1);
    expect("date", "-u -d @1 -r dated.txt", "", 1);
    expect("date", "--iso-8601=bogus", "", 1);
    expect("date", "--bogus", "", 1);
    expect("date", "-s \"2020-01-01\"", "", 1);
}

int main(void) {
    tools_init("clock_test");
    test_sleep();
    test_date();
    return test_summary("clock_test");
}
