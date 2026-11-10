/*
 * Tests of vdir, run as the program it is: it lists like "ls -l -b" (the long format, with blanks and backslashes written
 * as escapes).
 */

#include "testtools.h"

/** The names of the lines of a long listing (the fields before them differ from one machine to the other). */
static void check_names(const char* arguments, const char* names) {
    RunResult r;
    char got[1024];
    size_t used = 0;
    const char* p;

    run_tool(workDir, "vdir", arguments, "", &r);
    CHECK_INT(r.exitCode, 0);
    got[0] = '\0';
    for (p = r.out; p != NULL && *p != '\0';) {
        const char* end = strchr(p, '\n');
        size_t n = end != NULL ? (size_t)(end - p) : strlen(p);

        if (strncmp(p, "total ", 6) != 0) {
            // mode, links, owner, group, size, month, day, time: the name follows the eighth blank
            const char* name = p;
            int fields = 0;

            while (name < p + n && fields < 8) {
                if (*name == ' ') {
                    fields++;
                    while (name < p + n && *name == ' ')
                        name++;
                } else {
                    name++;
                }
            }
            if (used + (size_t)(p + n - name) + 2 < sizeof got) {
                memcpy(got + used, name, (size_t)(p + n - name));
                used += (size_t)(p + n - name);
                got[used++] = '\n';
                got[used] = '\0';
            }
        }
        p = end != NULL ? end + 1 : NULL;
    }
    CHECK_STR(got, names);
    run_result_free(&r);
}

int main(void) {
    RunResult r;

    tools_init("vdir_test");
    reset_dir(workDir);
    write_text(workDir, "a b", "xyz");
    write_text(workDir, "c.txt", "x");
    make_directory("sub");

    printf("vdir\n");
    check_names("", "a\\ b\nc.txt\nsub\n");
    run_tool(workDir, "vdir", "", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strstr(r.out, "total ") == r.out);                            // the long format
    CHECK(strstr(r.out, "-rw-r--r-- 1 ") != NULL);
    CHECK(strstr(r.out, "drwxr-xr-x 1 ") != NULL);
    CHECK(strstr(r.out, " 3 ") != NULL);                                // the size of "a b"
    run_result_free(&r);
    check_names("-r", "sub\nc.txt\na\\ b\n");
    check_names("-S", "a\\ b\nc.txt\nsub\n");
    check_names("-d sub", "sub\n");
    expect_run("vdir", "--version", "", "vdir (Winshell) 0.1\n", 0);
    return test_summary("vdir_test");
}
