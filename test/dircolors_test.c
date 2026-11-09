/*
 * Tests of dircolors, run as the program it is: the shell syntax, the TERM lines, the keywords and their codes, quoting,
 * the built-in database (-p) and the errors. GNU dircolors is compared with by gnu_compare.sh.
 */

#include "testtools.h"

static void with_term(const char* term, const char* arguments, const char* expected, int status) {
    RunResult r;
    char command[512];

    // the tool reads TERM and SHELL from its environment: env sets them
    snprintf(command, sizeof command, "TERM=%s SHELL=/bin/bash dircolors %s", term, arguments);
    run_tool(workDir, "env", command, "", &r);
    CHECK_INT(r.exitCode, status);
    CHECK_STR(r.out, expected);
    run_result_free(&r);
}

static void message(const char* term, const char* arguments, const char* part) {
    RunResult r;
    char command[512];

    snprintf(command, sizeof command, "TERM=%s SHELL=/bin/bash dircolors %s", term, arguments);
    run_tool(workDir, "env", command, "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

int main(void) {
    RunResult r;

    tools_init("dircolors_test");
    reset_dir(workDir);
    write_text(workDir, "db.txt", "TERM xterm*\nDIR 01;35\n.txt 00;32\n*.md 01;33 # comment\nLINK target\nOWT 1\nNORM 0\n");
    write_text(workDir, "plain.txt", "DIR 1\nFILE 2\n");
    write_text(workDir, "quote.txt", "DIR a'b\n.x:y 1:2\n");
    write_text(workDir, "terms.txt", "TERM a*\nDIR 1\nTERM b*\nDIR 2\nTERM c*\nFIFO 3\n");
    write_text(workDir, "badterm.txt", "TERM xterm\nHARDLINK 2\n");
    write_text(workDir, "bad.txt", "TERM xterm\nDIR 1\nHARDLINK 2\nBOGUS\nREG 3\n");

    printf("output\n");
    with_term("xterm", "-b db.txt", "LS_COLORS='di=01;35:*.txt=00;32:*.md=01;33:ln=target:tw=1:no=0:';\nexport LS_COLORS\n", 0);
    with_term("xterm", "-c db.txt", "setenv LS_COLORS 'di=01;35:*.txt=00;32:*.md=01;33:ln=target:tw=1:no=0:'\n", 0);
    with_term("vt100", "-b db.txt", "LS_COLORS='';\nexport LS_COLORS\n", 0);                   // another terminal: nothing
    with_term("x", "-b plain.txt", "LS_COLORS='di=1:fi=2:';\nexport LS_COLORS\n", 0);           // no TERM line: for every terminal
    with_term("x", "-b quote.txt", "LS_COLORS='di=a'\\''b:*.x\\:y=1\\:2:';\nexport LS_COLORS\n", 0);
    with_term("b1", "-b terms.txt", "LS_COLORS='di=2:';\nexport LS_COLORS\n", 0);
    with_term("c1", "-b terms.txt", "LS_COLORS='pi=3:';\nexport LS_COLORS\n", 0);
    with_term("xterm", "--sh plain.txt", "LS_COLORS='di=1:fi=2:';\nexport LS_COLORS\n", 0);
    with_term("xterm", "--c-shell plain.txt", "setenv LS_COLORS 'di=1:fi=2:'\n", 0);

    printf("the built-in database\n");
    run_tool(workDir, "env", "TERM=xterm SHELL=/bin/bash dircolors -b", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strncmp(r.out, "LS_COLORS='rs=0:di=01;34:ln=01;36:", 33) == 0);
    CHECK(strstr(r.out, ":*.tar=01;31:") != NULL && strstr(r.out, "';\nexport LS_COLORS\n") != NULL);
    run_result_free(&r);
    run_tool(workDir, "dircolors", "-p", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strstr(r.out, "# Configuration file for dircolors") == r.out);
    CHECK(strstr(r.out, "\nTERM xterm-256color\n") != NULL || strstr(r.out, "\nTERM *color*\n") != NULL);
    CHECK(strstr(r.out, "\nDIR 01;34 # directory\n") != NULL);
    run_result_free(&r);

    printf("errors\n");
    message("xterm", "-b bad.txt", "bad.txt:3: unrecognized keyword HARDLINK");
    message("xterm", "-b bad.txt", "bad.txt:4: invalid line;  missing second token");
    message("xterm", "-b bad.txt", "bad.txt:5: unrecognized keyword REG");
    message("xterm", "-b nosuchfile", "nosuchfile: No such file or directory");
    message("xterm", "-b plain.txt plain.txt", "extra operand 'plain.txt'");
    message("xterm", "-p plain.txt", "file operands cannot be combined with --print-database");
    message("xterm", "-b -p", "mutually exclusive");
    with_term("vt100", "-b badterm.txt", "LS_COLORS='';\nexport LS_COLORS\n", 0);                // an unknown keyword for another terminal is not an error
    expect_run("dircolors", "--version", "", "dircolors (Winshell) 0.1\n", 0);
    return test_summary("dircolors_test");
}
