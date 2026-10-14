/*
 * Tests of test, [, true and false, run as the programs they are: the status for strings, integers, files and
 * the expressions, the closing bracket, the messages. What GNU test says is compared by gnu_compare.sh.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void status_is(const char* tool, const char* arguments, int expected) {
    RunResult r;

    run_tool(workDir, tool, arguments, "", &r);
    if (r.exitCode != (DWORD)expected)
        printf("    for: %s %s\n    status %lu, expected %d\n    err: %s\n", tool, arguments, (unsigned long)r.exitCode,
               expected, r.err == NULL ? "" : r.err);
    CHECK_INT(r.exitCode, expected);
    CHECK_STR(r.out, "");
    run_result_free(&r);
}

static void t(const char* arguments, int expected) {
    status_is("test", arguments, expected);
}

static void test_arguments(void) {
    printf("by the number of arguments\n");
    t("", 1);
    t("x", 0);
    t("\"\"", 1);
    t("-n", 0);
    t("-z", 0);
    t("!", 0);
    t("! x", 1);
    t("! \"\"", 0);
    t("! !", 1);
    t("-n x", 0);
    t("-n \"\"", 1);
    t("-z \"\"", 0);
    t("-z x", 1);
    t("\"(\" x \")\"", 0);
    t("\"(\" \"\" \")\"", 1);
    t("! -n x", 1);

    printf("strings and integers\n");
    t("a = a", 0);
    t("a == a", 0);
    t("a = b", 1);
    t("a != b", 0);
    t("a != a", 1);
    t("a \"<\" b", 0);
    t("b \"<\" a", 1);
    t("b \">\" a", 0);
    t("2 -eq 2", 0);
    t("2 -ne 2", 1);
    t("1 -lt 2", 0);
    t("2 -le 2", 0);
    t("3 -gt 2", 0);
    t("2 -ge 3", 1);
    t("-1 -lt 0", 0);
    t("007 -eq 7", 0);
    t("\" 5\" -eq 5", 0);
    t("99999999999999999999 -gt 99999999999999999998", 0);
    t("-99999999999999999999 -lt -99999999999999999998", 0);
    t("-0 -eq 0", 0);

    printf("expressions\n");
    t("a -a b", 0);
    t("\"\" -a b", 1);
    t("\"\" -o b", 0);
    t("\"\" -o \"\"", 1);
    t("a -o \"\" -a \"\"", 0);                  // -a binds tighter than -o
    t("\"(\" a -o \"\" \")\" -a \"\"", 1);
    t("! \"(\" a -o b \")\"", 1);
    t("1 -eq 1 -a 2 -lt 3", 0);
    t("! 1 -eq 1", 1);
    t("-n -n", 0);
    t("= = =", 0);
}

static void test_files(void) {
    char path[MAX_PATH * 2];
    char arguments[MAX_PATH * 4];

    printf("files\n");
    write_text(workDir, "file.txt", "x\n");
    write_text(workDir, "empty.txt", "");
    make_directory("dir");

    t("-e file.txt", 0);
    t("-e nosuch", 1);
    t("-f file.txt", 0);
    t("-f dir", 1);
    t("-d dir", 0);
    t("-d file.txt", 1);
    t("-s file.txt", 0);
    t("-s empty.txt", 1);
    t("-r file.txt", 0);
    t("-r nosuch", 1);
    t("-w file.txt", 0);
    t("-x dir", 0);
    t("-L file.txt", 1);
    t("-b file.txt", 1);
    t("-O file.txt", 0);
    t("-e \"\"", 1);
    t("-d /", 0);

    // the extensions of PATHEXT make a file executable
    write_text(workDir, "run.cmd", "echo\n");
    t("-x run.cmd", 0);
    t("-x file.txt", 1);

    // read-only files are not writable
    snprintf(path, sizeof path, "%s\\ro.txt", workDir);
    write_text(workDir, "ro.txt", "x");
    SetFileAttributesA(path, FILE_ATTRIBUTE_READONLY);
    t("-w ro.txt", 1);
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);

    // newer, older and the same file; virtual paths
    write_text(workDir, "old.txt", "o");
    Sleep(50);
    write_text(workDir, "new.txt", "n");
    {
        SYSTEMTIME st;
        FILETIME earlier;
        HANDLE h;

        snprintf(path, sizeof path, "%s\\old.txt", workDir);
        h = CreateFileA(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        GetSystemTime(&st);
        st.wYear -= 1;
        SystemTimeToFileTime(&st, &earlier);
        SetFileTime(h, NULL, NULL, &earlier);
        CloseHandle(h);
    }
    t("new.txt -nt old.txt", 0);
    t("old.txt -nt new.txt", 1);
    t("old.txt -ot new.txt", 0);
    t("new.txt -nt nosuch", 0);
    t("nosuch -ot new.txt", 0);
    t("nosuch -nt new.txt", 1);
    t("file.txt -ef file.txt", 0);
    t("file.txt -ef ./file.txt", 0);
    t("file.txt -ef new.txt", 1);
    snprintf(arguments, sizeof arguments, "-f %s/file.txt", virtualWorkDir);
    t(arguments, 0);
}

static void test_errors(void) {
    RunResult r;

    printf("errors and the other names\n");
    run_tool(workDir, "test", "1 -eq", "", &r);
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "1: unary operator expected") != NULL);
    run_result_free(&r);

    run_tool(workDir, "test", "x -eq 1", "", &r);
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "x: integer expression expected") != NULL);
    run_result_free(&r);

    run_tool(workDir, "test", "a b", "", &r);
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "a: unary operator expected") != NULL);
    run_result_free(&r);

    run_tool(workDir, "test", "a b c", "", &r);
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "b: binary operator expected") != NULL);
    run_result_free(&r);

    run_tool(workDir, "test", "\"(\" a", "", &r);
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);

    // [ wants its closing bracket
    status_is("[", "a = a ]", 0);
    status_is("[", "a = b ]", 1);
    status_is("[", "-f file.txt ]", 0);
    status_is("[", "]", 1);
    run_tool(workDir, "[", "a = a", "", &r);
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "missing ']'") != NULL);
    run_result_free(&r);

    printf("true and false\n");
    status_is("true", "", 0);
    status_is("true", "a b c", 0);
    status_is("true", "--bogus", 0);
    status_is("false", "", 1);
    status_is("false", "a b c", 1);
    status_is("false", "--bogus", 1);
    run_tool(workDir, "true", "--version", "", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strncmp(r.out, "true ", 5) == 0);
    run_result_free(&r);
}

int main(void) {
    tools_init("test_cmd_test");
    test_arguments();
    test_files();
    test_errors();
    return test_summary("test_cmd_test");
}
