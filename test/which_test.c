/*
 * Tests of which, run as the program it is: the directory of the programs of the project, PATH, the
 * extensions, a name with a path, the output as virtual paths, and the exit status.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

int main(void) {
    RunResult r;
    char binDir[MAX_PATH], arguments[MAX_PATH * 3], expected[MAX_PATH * 3], virtualBin[MAX_PATH * 2];
    char path[MAX_PATH * 3];
    size_t i;

    tools_init("which_test");

    // the virtual form of the bin directory, as which shows it
    bin_path("sort", path, sizeof path);
    GetFullPathNameA(path, sizeof binDir, binDir, NULL);        // the path has \..\.. in it
    *strrchr(binDir, '\\') = '\0';
    expected_virtual(binDir, virtualBin, sizeof virtualBin);

    printf("the programs of the project\n");
    run_tool(workDir, "which", "sort", "", &r);
    snprintf(expected, sizeof expected, "%s/sort.exe\n", virtualBin);
    CHECK_STR(r.out, expected);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_tool(workDir, "which", "ls sort.exe", "", &r);
    snprintf(expected, sizeof expected, "%s/ls.exe\n%s/sort.exe\n", virtualBin, virtualBin);
    CHECK_STR(r.out, expected);
    run_result_free(&r);

    printf("PATH and the extensions\n");
    make_directory("tools");
    write_text(workDir, "tools\\mytool.exe", "x");
    write_text(workDir, "tools\\script.cmd", "x");
    write_text(workDir, "tools\\plain", "x");
    write_text(workDir, "tools\\plain.txt", "x");
    make_directory("tools\\adir.exe");

    {
        char pathValue[MAX_PATH * 3];
        snprintf(pathValue, sizeof pathValue, "PATH=%s\\tools", workDir);
        _putenv(pathValue);
        _putenv("PATHEXT=.EXE;.CMD");
    }
    snprintf(expected, sizeof expected, "%s/tools/mytool.exe\n", virtualWorkDir);
    run_tool(workDir, "which", "mytool", "", &r);
    CHECK_STR(r.out, expected);
    run_result_free(&r);

    snprintf(expected, sizeof expected, "%s/tools/script.cmd\n", virtualWorkDir);
    run_tool(workDir, "which", "script", "", &r);
    CHECK_STR(r.out, expected);
    run_result_free(&r);
    run_tool(workDir, "which", "script.cmd", "", &r);
    CHECK_STR(r.out, expected);
    run_result_free(&r);

    // a directory is not a program; a file without an extension of PATHEXT is not one either
    run_tool(workDir, "which", "adir", "", &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
    run_tool(workDir, "which", "plain", "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    // all matches, in the order of PATH
    make_directory("tools2");
    write_text(workDir, "tools2\\mytool.exe", "x");
    {
        char pathValue[MAX_PATH * 4];
        snprintf(pathValue, sizeof pathValue, "PATH=%s\\tools;%s\\tools2", workDir, workDir);
        _putenv(pathValue);
    }
    run_tool(workDir, "which", "-a mytool", "", &r);
    snprintf(expected, sizeof expected, "%s/tools/mytool.exe\n%s/tools2/mytool.exe\n", virtualWorkDir, virtualWorkDir);
    CHECK_STR(r.out, expected);
    run_result_free(&r);
    run_tool(workDir, "which", "mytool", "", &r);
    snprintf(expected, sizeof expected, "%s/tools/mytool.exe\n", virtualWorkDir);
    CHECK_STR(r.out, expected);
    run_result_free(&r);

    printf("names with a path\n");
    snprintf(arguments, sizeof arguments, "%s/tools/mytool", virtualWorkDir);
    run_tool(workDir, "which", arguments, "", &r);
    snprintf(expected, sizeof expected, "%s/tools/mytool.exe\n", virtualWorkDir);
    CHECK_STR(r.out, expected);
    run_result_free(&r);
    run_tool(workDir, "which", "tools/mytool.exe", "", &r);
    CHECK_STR(r.out, expected);
    run_result_free(&r);
    run_tool(workDir, "which", "tools/nosuch", "", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    printf("status\n");
    run_tool(workDir, "which", "mytool nosuchprogram_xyz", "", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "nosuchprogram_xyz") != NULL);
    run_result_free(&r);
    run_tool(workDir, "which", "-s mytool", "", &r);
    CHECK_STR(r.out, "");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    run_tool(workDir, "which", "-s nosuchprogram_xyz", "", &r);
    CHECK_STR(r.err, "");
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
    run_tool(workDir, "which", "--bogus", "", &r);
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);
    (void)i;

    return test_summary("which_test");
}
