/*
 * Tests of mktemp, run as the program it is: the shape of the names, files and directories that are made,
 * -u, --suffix, -p and the errors. GNU mktemp is compared with by gnu_compare.sh.
 */

#include <ctype.h>
#include "testtools.h"

/** Whether text is "prefix" + count random letters/digits + "suffix" and a line feed. */
static int has_shape(const char* text, const char* prefix, size_t count, const char* suffix) {
    size_t i, pl = strlen(prefix);

    if (text == NULL || strncmp(text, prefix, pl) != 0)
        return 0;
    for (i = 0; i < count; i++)
        if (!isalnum((unsigned char)text[pl + i]))
            return 0;
    return strcmp(text + pl + count, suffix) == 0;
}

static void check_made(const char* output, const char* prefix, size_t count, const char* suffix, int directory) {
    char name[MAX_PATH];
    DWORD attributes;
    size_t n;

    CHECK(has_shape(output, prefix, count, suffix));
    if (output == NULL)
        return;
    snprintf(name, sizeof name, "%s", output);
    n = strlen(name);
    while (n > 0 && (name[n - 1] == '\n' || name[n - 1] == '\r'))
        name[--n] = '\0';
    attributes = attributes_in(workDir, name);
    CHECK(attributes != INVALID_FILE_ATTRIBUTES);
    CHECK(((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory);
}

int main(void) {
    RunResult r;

    tools_init("mktemp_test");
    reset_dir(workDir);
    make_directory("sub");

    printf("files and directories\n");
    run_tool(workDir, "mktemp", "fileXXXXXX", "", &r);
    CHECK_INT(r.exitCode, 0);
    check_made(r.out, "file", 6, "\n", 0);
    run_result_free(&r);
    run_tool(workDir, "mktemp", "-d dirXXXXXX", "", &r);
    check_made(r.out, "dir", 6, "\n", 1);
    run_result_free(&r);
    run_tool(workDir, "mktemp", "--directory sub/dXXX", "", &r);
    check_made(r.out, "sub/d", 3, "\n", 1);
    run_result_free(&r);
    run_tool(workDir, "mktemp", "--suffix=.txt sXXXX", "", &r);
    check_made(r.out, "s", 4, ".txt\n", 0);
    run_result_free(&r);

    printf("-u makes nothing\n");
    run_tool(workDir, "mktemp", "-u dryXXXXXX", "", &r);
    CHECK(has_shape(r.out, "dry", 6, "\n"));
    if (r.out != NULL && r.out[0] != '\0') {
        r.out[strlen(r.out) - 1] = '\0';
        CHECK(!path_exists_in(workDir, r.out));
    }
    run_result_free(&r);

    printf("directories\n");
    run_tool(workDir, "mktemp", "-p sub pXXXXX", "", &r);
    check_made(r.out, "sub/p", 5, "\n", 0);
    run_result_free(&r);
    run_tool(workDir, "mktemp", "--tmpdir=sub qXXXXX", "", &r);
    check_made(r.out, "sub/q", 5, "\n", 0);
    run_result_free(&r);
    run_tool(workDir, "mktemp", "-u", "", &r);                 // no template: tmp.XXXXXXXXXX in the temporary directory
    CHECK(r.out != NULL && strstr(r.out, "/tmp.") != NULL && strlen(r.out) > 15);
    run_result_free(&r);

    printf("errors\n");
    expect_run("mktemp", "aXX", "", "", 1);                    // too few X
    expect_run("mktemp", "nothing", "", "", 1);
    expect_run("mktemp", "-q nosuchdir/aXXX", "", "", 1);
    expect_run("mktemp", "--suffix=a/b aXXX", "", "", 1);
    expect_run("mktemp", "a b", "", "", 1);
    expect_run("mktemp", "-x", "", "", 1);
    expect_run("mktemp", "--version", "", "mktemp (Winshell) 0.1\n", 0);

    return test_summary("mktemp_test");
}
