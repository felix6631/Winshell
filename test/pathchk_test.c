/*
 * Tests of pathchk, run as the program it is: the limits (255 and 4095 by default, 14 and 255 with -p), the portable
 * characters, -P (empty name, leading "-"), a file on the way, and the messages.
 * GNU pathchk is compared with by gnu_compare.sh (exit codes).
 */

#include "testtools.h"

static void expect(const char* arguments, int status) {
    expect_run("pathchk", arguments, "", "", status);
}

static void message(const char* arguments, const char* part) {
    RunResult r;

    run_tool(workDir, "pathchk", arguments, "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK_STR(r.out, "");
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

static void repeat(char* out, size_t size, char c, size_t n) {
    size_t i;

    for (i = 0; i < n && i + 1 < size; i++)
        out[i] = c;
    out[i] = '\0';
}

int main(void) {
    char name[600], arguments[1200];

    tools_init("pathchk_test");
    reset_dir(workDir);
    make_directory("d");
    write_text(workDir, "d\\file", "x");

    printf("ordinary names\n");
    expect("a", 0);
    expect("a/b/c", 0);
    expect("/a/b", 0);
    expect("a//b a/ ./a ../a", 0);
    expect("\"a b\" a:b", 0);
    expect("-- -a", 0);                                                  // a leading "-" is only for -P
    expect("d d/nosuch d/nosuch/deep /nosuchdir/x", 0);

    printf("limits\n");
    repeat(name, sizeof name, 'a', 255);
    snprintf(arguments, sizeof arguments, "%s", name);
    expect(arguments, 0);
    repeat(name, sizeof name, 'a', 256);
    snprintf(arguments, sizeof arguments, "%s", name);
    message(arguments, "limit 255 exceeded by length 256 of file name component");
    repeat(name, sizeof name, 'a', 14);
    snprintf(arguments, sizeof arguments, "-p %s", name);
    expect(arguments, 0);
    repeat(name, sizeof name, 'a', 15);
    snprintf(arguments, sizeof arguments, "-p a/%s", name);
    message(arguments, "limit 14 exceeded by length 15 of file name component");
    {
        int i;

        name[0] = '\0';
        for (i = 0; i < 90; i++)
            strcat(name, "ab/");
        snprintf(arguments, sizeof arguments, "-p %s", name);
        message(arguments, "limit 255 exceeded by length 270 of file name");
        snprintf(arguments, sizeof arguments, "%s", name);
        expect(arguments, 0);
    }

    printf("-p: portable characters\n");
    message("-p \"a b\"", "nonportable character ' ' in file name 'a b'");
    message("-p a:b", "nonportable character ':' in file name 'a:b'");
    expect("-p a.b_c-d/E1", 0);

    printf("-P: empty names and a leading -\n");
    message("-P \"\"", "empty file name");
    message("-p \"\"", "empty file name");
    message("\"\"", "No such file or directory");
    message("-P -- a/-b", "leading '-' in a component of file name 'a/-b'");
    message("--portability -- -a", "leading '-' in a component of file name '-a'");
    expect("-P a", 0);

    printf("a file on the way\n");
    message("d/file/x", "Not a directory");
    expect("-p d/file/x", 0);

    printf("several names and errors\n");
    expect("a b c", 0);
    expect("-p a \"\" c", 1);
    expect("", 1);
    expect("--bogus a", 1);
    expect_run("pathchk", "--version", "", "pathchk (Winshell) 0.1\n", 0);

    return test_summary("pathchk_test");
}
