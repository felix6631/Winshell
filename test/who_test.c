/*
 * Tests of who, users, uptime and pinky, run as the programs they are, on sessions that the test writes itself: a record is a
 * file pts<N> in the directory named by WINSHELL_SESSIONS with the process id, the start and the user (see
 * common/sessions.h); a record of a process that is gone does not count. The shell that registers itself is run once, with
 * WINSHELL_TEST_SESSION making it do so without a console. GNU who is compared with by gnu_compare.sh (headings, counts).
 */

#include "testtools.h"

static char sessionsDir[MAX_PATH];

static unsigned long long filetime_now(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

/** The time as the tools show it: "Nov 10 12:34", in local time. */
static void time_text(unsigned long long value, char* out, size_t size) {
    static const char* const months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    FILETIME utc, local;
    SYSTEMTIME t;

    utc.dwLowDateTime = (DWORD)(value & 0xffffffffULL);
    utc.dwHighDateTime = (DWORD)(value >> 32);
    FileTimeToLocalFileTime(&utc, &local);
    FileTimeToSystemTime(&local, &t);
    snprintf(out, size, "%s %2d %02d:%02d", months[t.wMonth - 1], t.wDay, t.wHour, t.wMinute);
}

static void write_record(int number, unsigned long pid, unsigned long long start, const char* user) {
    char name[64], text[256];
    snprintf(name, sizeof name, "pts%d", number);
    snprintf(text, sizeof text, "pid=%lu\nstart=%llu\nuser=%s\n", pid, start, user);
    write_text(sessionsDir, name, text);
}

static void expect(const char* tool, const char* arguments, const char* expected) {
    expect_run(tool, arguments, "", expected, 0);
}

int main(void) {
    char env[MAX_PATH + 32], t0[32], t1[32], line[512], expected[2048];
    unsigned long long now, later;
    RunResult r;

    tools_init("who_test");
    reset_dir(workDir);
    snprintf(sessionsDir, sizeof sessionsDir, "%s\\sessions", workDir);
    make_directory("sessions");
    snprintf(env, sizeof env, "WINSHELL_SESSIONS=%s", sessionsDir);
    _putenv(env);
    _putenv("WINSHELL_PTS=");

    printf("no sessions\n");
    expect("who", "", "");
    expect("users", "", "");
    expect("who", "-q", "\n# users=0\n");
    expect("who", "-H", "NAME     LINE         TIME         COMMENT\n");
    run_tool(workDir, "who", "-a -H", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strncmp(r.out, "NAME       LINE         TIME         IDLE          PID COMMENT  EXIT\n           system boot  ", 90) == 0);
    run_result_free(&r);
    run_tool(workDir, "who", "-b", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strncmp(r.out, "         system boot  ", 21) == 0);
    run_result_free(&r);

    now = filetime_now();
    later = now + 90ULL * 60ULL * 10000000ULL;                       // an hour and a half after
    time_text(now, t0, sizeof t0);
    time_text(later, t1, sizeof t1);
    write_record(0, GetCurrentProcessId(), now, "alice");
    write_record(1, GetCurrentProcessId(), later, "bob");
    write_record(2, 4294967290UL, now, "ghost");                     // no such process: not counted
    write_text(sessionsDir, "pts3", "garbage\n");                    // not a record
    write_text(sessionsDir, "other.txt", "x");

    printf("who\n");
    snprintf(expected, sizeof expected, "alice    pts/0        %s\nbob      pts/1        %s\n", t0, t1);
    expect("who", "", expected);
    expect("who", "-s", expected);
    snprintf(expected, sizeof expected, "NAME     LINE         TIME         COMMENT\nalice    pts/0        %s\nbob      pts/1        %s\n", t0, t1);
    expect("who", "-H", expected);
    snprintf(expected, sizeof expected, "alice    ? pts/0        %s\nbob      ? pts/1        %s\n", t0, t1);
    expect("who", "-T", expected);
    snprintf(expected, sizeof expected, "alice    pts/0        %s   ?  %10lu\nbob      pts/1        %s   ?  %10lu\n", t0,
             (unsigned long)GetCurrentProcessId(), t1, (unsigned long)GetCurrentProcessId());
    snprintf(line, sizeof line, "alice    pts/0        %s   ?    %10lu", t0, (unsigned long)GetCurrentProcessId());
    run_tool(workDir, "who", "-u", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strncmp(r.out, line, strlen(line)) == 0);
    run_result_free(&r);
    expect("who", "-q", "alice bob\n# users=2\n");
    expect("who", "-d", "");                                          // no dead processes, logins, run levels ...
    expect("who", "-r", "");
    expect("who", "-t", "");
    expect("who", "-l", "");
    expect("who", "-p", "");
    snprintf(expected, sizeof expected, "alice    pts/0        %s\nbob      pts/1        %s\n", t0, t1);
    expect("who", "nosuchfile", expected);                              // the file is not read
    expect("users", "", "alice bob\n");
    expect("users", "nosuchfile", "alice bob\n");

    printf("who am i\n");
    _putenv("WINSHELL_PTS=pts/1");
    snprintf(expected, sizeof expected, "bob      pts/1        %s\n", t1);
    expect("who", "am i", expected);
    expect("who", "-m", expected);
    _putenv("WINSHELL_PTS=");
    expect("who", "am i", "");                                        // not in a shell of Winshell: nothing

    printf("uptime\n");
    run_tool(workDir, "uptime", "", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(r.out[0] == ' ' && r.out[3] == ':' && r.out[6] == ':' && r.out[9] == ' ');           // " HH:MM:SS "
    CHECK(strstr(r.out, " up ") != NULL);
    CHECK(strstr(r.out, ",  2 users,  load average: 0.00, 0.00, 0.00\n") != NULL);
    run_result_free(&r);
    expect_run("uptime", "a b", "", "", 1);

    printf("pinky\n");
    run_tool(workDir, "pinky", "", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strncmp(r.out, "Login    Name                 TTY      Idle   When         Where\n", 62) == 0);
    snprintf(line, sizeof line, "%-8s %-19.19s  ?%-8s %-6s %s\n", "alice", "alice", "pts/0", "?", t0);
    CHECK(strstr(r.out, line) != NULL);
    run_result_free(&r);
    expect("pinky", "-f alice", line);
    expect("pinky", "-f carol", "");
    snprintf(expected, sizeof expected, "%-8s  ?%-8s %s\n%-8s  ?%-8s %s\n", "alice", "pts/0", t0, "bob", "pts/1", t1);
    expect("pinky", "-f -q", expected);
    expect_run("pinky", "-l", "", "", 1);
    expect_run("who", "a b c", "", "", 1);
    expect_run("who", "--bogus", "", "", 1);
    expect_run("who", "--version", "", "who (Winshell) 0.1\n", 0);

    printf("the shell registers itself\n");
    reset_dir(sessionsDir);
    _putenv("WINSHELL_TEST_SESSION=1");
    {
        RunResult s;

        script("who\nwho -m\necho $WINSHELL_PTS\nusers\nwho -q\n", &s);
        CHECK_INT(s.exitCode, 0);
        CHECK(strstr(s.out, "pts/0        ") != NULL);
        CHECK(strstr(s.out, "\npts/0\n") != NULL);
        CHECK(strstr(s.out, "# users=1") != NULL);
        run_result_free(&s);
    }
    _putenv("WINSHELL_TEST_SESSION=");
    expect("who", "", "");                                            // the record went with the shell
    return test_summary("who_test");
}
