/*
 * Tests of kill and ps, run as the programs they are against real processes: sleep started by the test.
 * ps: the selections (-p, -C, --ppid, -e, -N), the formats (default, -f, u, -o with headings), sorting, and
 * the exit status. kill: the signal names and numbers, -l and -L, the signals that end a process (with the
 * exit status 128 + the number), suspend and resume, and the errors.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

#define WAIT 5000

static void start_sleep(Process* p, DWORD* pid) {
    char path[MAX_PATH * 2];

    bin_path("sleep", path, sizeof path);
    CHECK(proc_start(p, path, "60", workDir) == 0);
    *pid = GetProcessId(p->process);
}

static void format_number(char* out, size_t size, const char* before, DWORD number, const char* after) {
    snprintf(out, size, "%s%lu%s", before, number, after);
}

/** The words of one line of the output, as the first line that contains the text. */
static const char* line_with(const char* text, const char* needle) {
    return strstr(text, needle);
}

static void test_ps(void) {
    Process p, q;
    DWORD pid, other;
    RunResult r;
    char arguments[256], expected[256];

    printf("ps selection and formats\n");
    start_sleep(&p, &pid);
    start_sleep(&q, &other);

    format_number(arguments, sizeof arguments, "-p ", pid, " -o pid=,comm=");
    run_tool(workDir, "ps", arguments, "", &r);
    snprintf(expected, sizeof expected, "%7lu sleep.exe\n", pid);
    CHECK_STR(r.out, expected);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    // the default format: the heading, and the columns PID TTY TIME CMD
    format_number(arguments, sizeof arguments, "-p ", pid, "");
    run_tool(workDir, "ps", arguments, "", &r);
    CHECK(strncmp(r.out, "    PID TTY          TIME CMD\n", 29) == 0);
    snprintf(expected, sizeof expected, "%7lu ", pid);
    CHECK(line_with(r.out, expected) != NULL);
    CHECK(line_with(r.out, " sleep.exe\n") != NULL);
    run_result_free(&r);

    // the full format has the command line
    format_number(arguments, sizeof arguments, "-f -p ", pid, "");
    run_tool(workDir, "ps", arguments, "", &r);
    // the UID column is as wide as the longest user name (a CI account has a long one)
    CHECK(strncmp(r.out, "UID ", 4) == 0);
    CHECK(strstr(r.out, " PID    PPID  C STIME TTY          TIME CMD\n") != NULL);
    CHECK(line_with(r.out, "sleep") != NULL);
    CHECK(line_with(r.out, " 60") != NULL);
    run_result_free(&r);

    format_number(arguments, sizeof arguments, "-o args= -p ", pid, "");
    run_tool(workDir, "ps", arguments, "", &r);
    CHECK(strstr(r.out, "sleep") != NULL && strstr(r.out, "60") != NULL);
    run_result_free(&r);

    // BSD style
    format_number(arguments, sizeof arguments, "u p ", pid, "");
    run_tool(workDir, "ps", arguments, "", &r);
    CHECK(strncmp(r.out, "USER ", 5) == 0);
    CHECK(line_with(r.out, "%CPU %MEM") != NULL);
    run_result_free(&r);

    // headings: renamed, and none when every heading is empty
    format_number(arguments, sizeof arguments, "-p ", pid, " -o pid=ID -o comm=NAME");
    run_tool(workDir, "ps", arguments, "", &r);
    CHECK(strncmp(r.out, "     ID NAME\n", 13) == 0);
    run_result_free(&r);
    format_number(arguments, sizeof arguments, "-p ", pid, " -o pid=,comm=");
    run_tool(workDir, "ps", arguments, "", &r);
    CHECK(strchr(r.out, '\n') == r.out + strlen(r.out) - 1);        // one line only
    run_result_free(&r);
    format_number(arguments, sizeof arguments, "--no-headers -p ", pid, " -o pid");
    run_tool(workDir, "ps", arguments, "", &r);
    snprintf(expected, sizeof expected, "%7lu\n", pid);
    CHECK_STR(r.out, expected);
    run_result_free(&r);

    // by command name, by parent, several pids, deselect
    run_tool(workDir, "ps", "-C sleep -o pid=", "", &r);
    snprintf(expected, sizeof expected, "%7lu", pid);
    CHECK(strstr(r.out, expected) != NULL);
    snprintf(expected, sizeof expected, "%7lu", other);
    CHECK(strstr(r.out, expected) != NULL);
    run_result_free(&r);

    snprintf(arguments, sizeof arguments, "--ppid %lu -o pid=", GetCurrentProcessId());
    run_tool(workDir, "ps", arguments, "", &r);
    snprintf(expected, sizeof expected, "%7lu", pid);
    CHECK(strstr(r.out, expected) != NULL);
    run_result_free(&r);

    snprintf(arguments, sizeof arguments, "-p %lu,%lu -o pid= --sort=-pid", pid, other);
    run_tool(workDir, "ps", arguments, "", &r);
    if (pid > other)
        snprintf(expected, sizeof expected, "%7lu\n%7lu\n", pid, other);
    else
        snprintf(expected, sizeof expected, "%7lu\n%7lu\n", other, pid);
    CHECK_STR(r.out, expected);
    run_result_free(&r);
    snprintf(arguments, sizeof arguments, "-p %lu,%lu -o pid= --sort=pid", pid, other);
    run_tool(workDir, "ps", arguments, "", &r);
    if (pid > other)
        snprintf(expected, sizeof expected, "%7lu\n%7lu\n", other, pid);
    else
        snprintf(expected, sizeof expected, "%7lu\n%7lu\n", pid, other);
    CHECK_STR(r.out, expected);
    run_result_free(&r);

    snprintf(arguments, sizeof arguments, "-N -p %lu -o pid=", pid);
    run_tool(workDir, "ps", arguments, "", &r);
    snprintf(expected, sizeof expected, "%7lu\n", pid);
    CHECK(strstr(r.out, expected) == NULL);
    CHECK(r.out[0] != '\0');
    run_result_free(&r);

    // all processes include this test and the sleeps
    run_tool(workDir, "ps", "-e -o pid=", "", &r);
    snprintf(expected, sizeof expected, "%7lu\n", GetCurrentProcessId());
    CHECK(strstr(r.out, expected) != NULL);
    run_result_free(&r);
    run_tool(workDir, "ps", "ax -o pid=", "", &r);
    CHECK(strstr(r.out, expected) != NULL);
    run_result_free(&r);

    // memory and times are numbers for a process of ours
    format_number(arguments, sizeof arguments, "-p ", pid, " -o rss=,vsz=,nlwp=,etime=,start=");
    run_tool(workDir, "ps", arguments, "", &r);
    CHECK(strstr(r.out, "-") == NULL);
    CHECK(strstr(r.out, "00:") != NULL);                            // elapsed: minutes and seconds
    run_result_free(&r);

    printf("ps status and errors\n");
    run_tool(workDir, "ps", "-p 2147483640", "", &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
    run_tool(workDir, "ps", "-p abc", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(r.err[0] != '\0');
    run_result_free(&r);
    run_tool(workDir, "ps", "-o nosuchcolumn", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "nosuchcolumn") != NULL);
    run_result_free(&r);
    run_tool(workDir, "ps", "--bogus", "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    proc_kill(&p, &r);
    run_result_free(&r);
    proc_kill(&q, &r);
    run_result_free(&r);
}

static void test_kill(void) {
    Process p;
    DWORD pid;
    RunResult r;
    char arguments[256];

    printf("kill names and lists\n");
    run_tool(workDir, "kill", "-l", "", &r);
    CHECK(strncmp(r.out, "HUP INT QUIT ILL TRAP ABRT BUS FPE KILL USR1 SEGV USR2 PIPE ALRM TERM", 68) == 0);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    run_tool(workDir, "kill", "-l 9 15 137 INT sigterm", "", &r);
    CHECK_STR(r.out, "KILL\nTERM\nKILL\n2\n15\n");
    run_result_free(&r);
    run_tool(workDir, "kill", "-l NOSUCH", "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
    run_tool(workDir, "kill", "-L", "", &r);
    CHECK(strncmp(r.out, " 1 HUP\n 2 INT\n", 14) == 0);
    run_result_free(&r);

    printf("kill errors\n");
    run_tool(workDir, "kill", "2147483640", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "No such process") != NULL);
    run_result_free(&r);
    run_tool(workDir, "kill", "abc", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "failed to parse argument: 'abc'") != NULL);
    run_result_free(&r);
    run_tool(workDir, "kill", "-0 0", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "process groups") != NULL);
    run_result_free(&r);
    run_tool(workDir, "kill", "-s NOSUCH 1", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "NOSUCH") != NULL);
    run_result_free(&r);
    run_tool(workDir, "kill", "", "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    printf("kill sends signals\n");
    start_sleep(&p, &pid);
    format_number(arguments, sizeof arguments, "-0 ", pid, "");
    run_tool(workDir, "kill", arguments, "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK_STR(r.out, "");
    run_result_free(&r);

    // stop and continue keep the process, a signal that is ignored does nothing
    format_number(arguments, sizeof arguments, "-STOP ", pid, "");
    run_tool(workDir, "kill", arguments, "", &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    format_number(arguments, sizeof arguments, "-s CONT ", pid, "");
    run_tool(workDir, "kill", arguments, "", &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    format_number(arguments, sizeof arguments, "-WINCH ", pid, "");
    run_tool(workDir, "kill", arguments, "", &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    CHECK(!proc_finished(&p));

    // TERM is the default: a process without windows ends with 128 + 15
    format_number(arguments, sizeof arguments, "", pid, "");
    run_tool(workDir, "kill", arguments, "", &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_INT(r.exitCode, 143);
    run_result_free(&r);

    // KILL, named in the ways of the options
    start_sleep(&p, &pid);
    format_number(arguments, sizeof arguments, "-s SIGKILL ", pid, "");
    run_tool(workDir, "kill", arguments, "", &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_INT(r.exitCode, 137);
    run_result_free(&r);

    start_sleep(&p, &pid);
    format_number(arguments, sizeof arguments, "-9 ", pid, "");
    run_tool(workDir, "kill", arguments, "", &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_INT(r.exitCode, 137);
    run_result_free(&r);

    // a process that has ended is gone
    format_number(arguments, sizeof arguments, "-0 ", pid, "");
    run_tool(workDir, "kill", arguments, "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    // several pids: the status is 1 when one failed, the others are still signalled
    start_sleep(&p, &pid);
    snprintf(arguments, sizeof arguments, "-KILL 2147483640 %lu", pid);
    run_tool(workDir, "kill", arguments, "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
    CHECK(proc_wait(&p, WAIT, &r) == 0);
    CHECK_INT(r.exitCode, 137);
    run_result_free(&r);
}

int main(void) {
    tools_init("proc_test");
    test_ps();
    test_kill();
    return test_summary("proc_test");
}
