/*
 * Tests of mkdir, rmdir and touch.
 */

#include "testtools.h"

static void test_mkdir(void) {
    RunResult r;
    char d[MAX_PATH * 2], arguments[MAX_PATH * 3];

    printf("mkdir\n");
    snprintf(d, sizeof d, "%s\\mkdir", workDir);
    reset_dir(d);

    run_tool(d, "mkdir", "a", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK((attributes_in(d, "a") & FILE_ATTRIBUTE_DIRECTORY) != 0);
    run_result_free(&r);

    run_tool(d, "mkdir", "a", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "mkdir: cannot create directory 'a': File exists") != NULL);
    run_result_free(&r);

    run_tool(d, "mkdir", "-p a", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK_STR(r.err, "");
    run_result_free(&r);

    run_tool(d, "mkdir", "x y z", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(path_exists_in(d, "x") && path_exists_in(d, "y") && path_exists_in(d, "z"));
    run_result_free(&r);

    // one failure does not stop the others, the exit code tells
    run_tool(d, "mkdir", "x new1 new2", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(path_exists_in(d, "new1") && path_exists_in(d, "new2"));
    run_result_free(&r);

    run_tool(d, "mkdir", "nosuch\\sub", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot create directory 'nosuch\\sub': No such file or directory") != NULL);
    CHECK(!path_exists_in(d, "nosuch"));
    run_result_free(&r);

    // parents
    run_tool(d, "mkdir", "-p p1\\p2\\p3", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(path_exists_in(d, "p1\\p2\\p3"));
    run_result_free(&r);

    run_tool(d, "mkdir", "-p p1\\p2\\p3\\p4", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(path_exists_in(d, "p1\\p2\\p3\\p4"));
    run_result_free(&r);

    run_tool(d, "mkdir", "-pv v1/v2/v3", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strstr(r.out, "mkdir: created directory 'v1'") != NULL);
    CHECK(strstr(r.out, "mkdir: created directory 'v1/v2'") != NULL);
    CHECK(strstr(r.out, "mkdir: created directory 'v1/v2/v3'") != NULL);
    run_result_free(&r);

    run_tool(d, "mkdir", "-v single", NULL, &r);
    CHECK_STR(lf_only(r.out), "mkdir: created directory 'single'\n");
    run_result_free(&r);

    // a file in the way
    write_text(d, "f.txt", "x");
    run_tool(d, "mkdir", "f.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "File exists") != NULL);
    run_result_free(&r);
    run_tool(d, "mkdir", "-p f.txt\\sub", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "File exists") != NULL);
    run_result_free(&r);

    // the mode has no meaning here and is skipped, in all of its spellings
    run_tool(d, "mkdir", "-m 755 m1", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(path_exists_in(d, "m1"));
    run_result_free(&r);
    run_tool(d, "mkdir", "-m755 m2", NULL, &r);
    CHECK(path_exists_in(d, "m2"));
    run_result_free(&r);
    run_tool(d, "mkdir", "--mode=700 m3", NULL, &r);
    CHECK(path_exists_in(d, "m3"));
    run_result_free(&r);

    // names Windows does not allow
    run_tool(d, "mkdir", "\"bad*name\"", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "Invalid argument") != NULL);
    run_result_free(&r);

    // virtual paths
    snprintf(arguments, sizeof arguments, "\"%s/virt/sub\" -p", virtualWorkDir);
    snprintf(arguments, sizeof arguments, "-p \"%s/mkdir/virt/sub\"", virtualWorkDir);
    run_tool(d, "mkdir", arguments, NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(path_exists_in(d, "virt\\sub"));
    run_result_free(&r);

    // the root only holds the drives
    run_tool(d, "mkdir", "/", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot create directory '/': File exists") != NULL);
    run_result_free(&r);
    run_tool(d, "mkdir", "-p /", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    run_tool(d, "mkdir", "/nodrive", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "Permission denied") != NULL);
    run_result_free(&r);

    run_tool(d, "mkdir", "", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "mkdir: missing operand") != NULL);
    run_result_free(&r);

    run_tool(d, "mkdir", "-x a", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "invalid option -- 'x'") != NULL);
    run_result_free(&r);

    run_tool(d, "mkdir", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: mkdir") != NULL);
    run_result_free(&r);
}

static void test_rmdir(void) {
    RunResult r;
    char d[MAX_PATH * 2];

    printf("rmdir\n");
    snprintf(d, sizeof d, "%s\\rmdir", workDir);
    reset_dir(d);

    make_dir_in(d, "empty");
    run_tool(d, "rmdir", "empty", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "empty"));
    run_result_free(&r);

    make_dir_in(d, "full");
    write_text(d, "full\\f.txt", "x");
    run_tool(d, "rmdir", "full", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rmdir: failed to remove 'full': Directory not empty") != NULL);
    CHECK(path_exists_in(d, "full\\f.txt"));
    run_result_free(&r);

    run_tool(d, "rmdir", "--ignore-fail-on-non-empty full", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(path_exists_in(d, "full"));
    run_result_free(&r);

    run_tool(d, "rmdir", "nosuch", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rmdir: failed to remove 'nosuch': No such file or directory") != NULL);
    run_result_free(&r);

    run_tool(d, "rmdir", "full\\f.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "Not a directory") != NULL);
    CHECK(path_exists_in(d, "full\\f.txt"));
    run_result_free(&r);

    // several, one failing
    make_dir_in(d, "e1");
    make_dir_in(d, "e2");
    run_tool(d, "rmdir", "e1 full e2", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(!path_exists_in(d, "e1") && !path_exists_in(d, "e2"));
    run_result_free(&r);

    // verbose
    make_dir_in(d, "vv");
    run_tool(d, "rmdir", "-v vv", NULL, &r);
    CHECK_STR(lf_only(r.out), "rmdir: removing directory, 'vv'\n");
    run_result_free(&r);

    // a read-only directory goes too
    make_dir_in(d, "ro");
    SetFileAttributesA("ro", FILE_ATTRIBUTE_NORMAL);
    {
        char path[MAX_PATH * 2];
        snprintf(path, sizeof path, "%s\\ro", d);
        SetFileAttributesA(path, FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY);
    }
    run_tool(d, "rmdir", "ro", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "ro"));
    run_result_free(&r);

    // parents
    make_dir_in(d, "pa");
    make_dir_in(d, "pa\\pb");
    make_dir_in(d, "pa\\pb\\pc");
    run_tool(d, "rmdir", "-pv pa/pb/pc", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "pa"));
    CHECK(strstr(r.out, "removing directory, 'pa/pb/pc'") != NULL);
    CHECK(strstr(r.out, "removing directory, 'pa/pb'") != NULL);
    CHECK(strstr(r.out, "removing directory, 'pa'") != NULL);
    run_result_free(&r);

    // ... until a directory is not empty
    make_dir_in(d, "qa");
    make_dir_in(d, "qa\\qb");
    write_text(d, "qa\\keep.txt", "x");
    run_tool(d, "rmdir", "-p qa\\qb", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(!path_exists_in(d, "qa\\qb"));
    CHECK(path_exists_in(d, "qa\\keep.txt"));
    CHECK(strstr(r.err, "failed to remove 'qa': Directory not empty") != NULL);
    run_result_free(&r);

    // roots
    run_tool(d, "rmdir", "/", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "Device or resource busy") != NULL);
    run_result_free(&r);

    {
        char arguments[16];
        snprintf(arguments, sizeof arguments, "/%c", virtualWorkDir[1]);
        run_tool(d, "rmdir", arguments, NULL, &r);
        CHECK_INT(r.exitCode, 1);
        run_result_free(&r);
    }

    run_tool(d, "rmdir", "", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rmdir: missing operand") != NULL);
    run_result_free(&r);

    run_tool(d, "rmdir", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: rmdir") != NULL);
    run_result_free(&r);
}

static void test_touch(void) {
    RunResult r;
    char d[MAX_PATH * 2], arguments[MAX_PATH * 3];
    SYSTEMTIME before, after, t;

    printf("touch\n");
    snprintf(d, sizeof d, "%s\\touch", workDir);
    reset_dir(d);

    // a new empty file, with the time of now
    GetSystemTime(&before);
    run_tool(d, "touch", "new.txt", NULL, &r);
    GetSystemTime(&after);
    CHECK_INT(r.exitCode, 0);
    CHECK(path_exists_in(d, "new.txt"));
    {
        size_t length = 1;
        char* text = read_file(d, "new.txt", &length);
        CHECK_INT(length, 0);
        free(text);
    }
    CHECK(time_of(d, "new.txt", 2, &t) && t.wYear >= before.wYear && t.wYear <= after.wYear);
    run_result_free(&r);

    // an existing file keeps its content
    write_text(d, "data.txt", "content");
    set_mtime(d, "data.txt", 2001, 6, 15);
    run_tool(d, "touch", "data.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "data.txt", "content");
    CHECK(year_of(d, "data.txt") > 2001);
    run_result_free(&r);

    // -c
    run_tool(d, "touch", "-c nothing.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "nothing.txt"));
    run_result_free(&r);

    // given times
    run_tool(d, "touch", "-d 2001-06-15T12:30:45Z a.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_time(d, "a.txt", 2, 2001, 6, 15, 12, 30, 45);
    expect_time(d, "a.txt", 1, 2001, 6, 15, 12, 30, 45);
    run_result_free(&r);

    run_tool(d, "touch", "-d \"2002-01-02 03:04:05Z\" b.txt", NULL, &r);
    expect_time(d, "b.txt", 2, 2002, 1, 2, 3, 4, 5);
    run_result_free(&r);

    run_tool(d, "touch", "-d 2003-03-04Z c.txt", NULL, &r);
    expect_time(d, "c.txt", 2, 2003, 3, 4, 0, 0, 0);
    run_result_free(&r);

    run_tool(d, "touch", "--date=@86400 e.txt", NULL, &r);
    expect_time(d, "e.txt", 2, 1970, 1, 2, 0, 0, 0);
    run_result_free(&r);

    // a time without Z is local time: the month is safe from any difference to UTC
    run_tool(d, "touch", "-t 200406151200.30 f.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    {
        SYSTEMTIME s;
        CHECK(time_of(d, "f.txt", 2, &s) && s.wYear == 2004 && s.wMonth == 6);
    }
    run_result_free(&r);

    run_tool(d, "touch", "-t 06151200 g.txt", NULL, &r);      // MMDDhhmm without a year is this year
    CHECK_INT(r.exitCode, 0);
    {
        SYSTEMTIME s, now;
        GetSystemTime(&now);
        CHECK(time_of(d, "g.txt", 2, &s) && s.wMonth == 6 && (s.wYear == now.wYear || s.wYear == now.wYear - 1));
    }
    run_result_free(&r);

    run_tool(d, "touch", "-t 9906151200 g2.txt", NULL, &r);   // YYMMDDhhmm: 99 is 1999, 05 would be 2005
    CHECK_INT(r.exitCode, 0);
    {
        SYSTEMTIME s;
        CHECK(time_of(d, "g2.txt", 2, &s) && s.wYear == 1999 && s.wMonth == 6);
    }
    run_result_free(&r);

    run_tool(d, "touch", "-t 1306151200 g3.txt", NULL, &r);
    {
        SYSTEMTIME s;
        CHECK(time_of(d, "g3.txt", 2, &s) && s.wYear == 2013);
    }
    run_result_free(&r);

    run_tool(d, "touch", "-t 0615120 g4.txt", NULL, &r);      // 7 digits are no time stamp
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    // -a and -m change one time only
    set_mtime(d, "a.txt", 2005, 5, 5);
    run_tool(d, "touch", "-a -d 2001-06-15T12:00:00Z a.txt", NULL, &r);
    expect_time(d, "a.txt", 1, 2001, 6, 15, 12, 0, 0);
    expect_time(d, "a.txt", 2, 2005, 5, 5, 12, 0, 0);
    run_result_free(&r);

    run_tool(d, "touch", "-m -d 2010-10-10T10:10:10Z a.txt", NULL, &r);
    expect_time(d, "a.txt", 2, 2010, 10, 10, 10, 10, 10);
    expect_time(d, "a.txt", 1, 2001, 6, 15, 12, 0, 0);
    run_result_free(&r);

    // a reference file
    run_tool(d, "touch", "-r b.txt h.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_time(d, "h.txt", 2, 2002, 1, 2, 3, 4, 5);
    run_result_free(&r);

    // directories
    make_dir_in(d, "dir");
    run_tool(d, "touch", "-d 2001-06-15T12:00:00Z dir", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_time(d, "dir", 2, 2001, 6, 15, 12, 0, 0);
    run_result_free(&r);

    // several files, a read-only one among them
    make_sized_file(d, "ro.txt", 1, FILE_ATTRIBUTE_READONLY);
    run_tool(d, "touch", "-d 2001-06-15T12:00:00Z ro.txt s1.txt s2.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_time(d, "ro.txt", 2, 2001, 6, 15, 12, 0, 0);
    CHECK(path_exists_in(d, "s1.txt") && path_exists_in(d, "s2.txt"));
    CHECK((attributes_in(d, "ro.txt") & FILE_ATTRIBUTE_READONLY) != 0);
    run_result_free(&r);

    // virtual paths
    snprintf(arguments, sizeof arguments, "\"%s/touch/virt.txt\"", virtualWorkDir);
    run_tool(d, "touch", arguments, NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(path_exists_in(d, "virt.txt"));
    run_result_free(&r);

    // errors
    run_tool(d, "touch", "nodir\\x.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "touch: cannot touch 'nodir\\x.txt': No such file or directory") != NULL);
    run_result_free(&r);

    run_tool(d, "touch", "-d garbage x.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "touch: invalid date format 'garbage'") != NULL);
    CHECK(!path_exists_in(d, "x.txt"));
    run_result_free(&r);

    run_tool(d, "touch", "-d 2001-13-40 x.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    run_tool(d, "touch", "-r nosuch.txt x.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "failed to get attributes of 'nosuch.txt'") != NULL);
    run_result_free(&r);

    run_tool(d, "touch", "", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "touch: missing file operand") != NULL);
    run_result_free(&r);

    run_tool(d, "touch", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: touch") != NULL);
    run_result_free(&r);
}

int main(void) {
    tools_init("dirs_test");

    test_mkdir();
    test_rmdir();
    test_touch();
    return test_summary("dirs_test");
}
