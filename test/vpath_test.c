/*
 * Unit tests of common/vpath.c: the conversion between the virtual file
 * system (/c/Users/me) and Windows paths (C:\Users\me).
 */

#include <errno.h>
#include "testlib.h"
#include "vpath.h"

static char windowsDir[MAX_PATH];   // e.g. C:\Windows
static char driveRoot[8];           // the root of the system drive: C, a colon and a backslash
static char virtualWindowsDir[MAX_PATH];

static void test_from_native(void) {
    char out[VPATH_MAX];

    printf("vpath_from_native\n");
    CHECK_INT(vpath_from_native("C:\\Users\\me", out, sizeof out), 0);
    CHECK_STR(out, "/c/Users/me");

    CHECK_INT(vpath_from_native("c:/Users/me", out, sizeof out), 0);
    CHECK_STR(out, "/c/Users/me");

    CHECK_INT(vpath_from_native("D:\\", out, sizeof out), 0);
    CHECK_STR(out, "/d");

    CHECK_INT(vpath_from_native("D:", out, sizeof out), 0);
    CHECK_STR(out, "/d");

    CHECK_INT(vpath_from_native("C:\\a\\..\\b\\.\\c\\", out, sizeof out), 0);
    CHECK_STR(out, "/c/b/c");

    // outside of the drives
    CHECK_INT(vpath_from_native("\\\\server\\share\\x", out, sizeof out), -1);
    CHECK_INT(vpath_from_native("relative\\path", out, sizeof out), -1);
}

static void test_resolve(void) {
    char out[VPATH_MAX];

    printf("vpath_resolve\n");
    CHECK_INT(vpath_resolve("/c/Users", ".", out, sizeof out), 0);
    CHECK_STR(out, "/c/Users");

    CHECK_INT(vpath_resolve("/c/Users", "..", out, sizeof out), 0);
    CHECK_STR(out, "/c");

    CHECK_INT(vpath_resolve("/c/Users", "../..", out, sizeof out), 0);
    CHECK_STR(out, "/");

    // the root is its own parent
    CHECK_INT(vpath_resolve("/c/Users", "../../../..", out, sizeof out), 0);
    CHECK_STR(out, "/");
    CHECK_INT(vpath_resolve("/", "..", out, sizeof out), 0);
    CHECK_STR(out, "/");

    CHECK_INT(vpath_resolve("/c/Users", "a/b", out, sizeof out), 0);
    CHECK_STR(out, "/c/Users/a/b");

    CHECK_INT(vpath_resolve("/c/Users", "a\\b", out, sizeof out), 0);
    CHECK_STR(out, "/c/Users/a/b");

    CHECK_INT(vpath_resolve("/c/Users", "a/", out, sizeof out), 0);
    CHECK_STR(out, "/c/Users/a");

    CHECK_INT(vpath_resolve("/c/Users", "./a/./b/../c", out, sizeof out), 0);
    CHECK_STR(out, "/c/Users/a/c");

    // absolute paths ignore the base
    CHECK_INT(vpath_resolve("/c/Users", "/d/x/../y", out, sizeof out), 0);
    CHECK_STR(out, "/d/y");

    CHECK_INT(vpath_resolve("/c/Users", "/", out, sizeof out), 0);
    CHECK_STR(out, "/");

    // the drive is spelled in lower case, names keep their case
    CHECK_INT(vpath_resolve("/", "/C/Dir/File", out, sizeof out), 0);
    CHECK_STR(out, "/c/Dir/File");

    // Windows notation
    CHECK_INT(vpath_resolve("/d", "C:\\Windows\\..", out, sizeof out), 0);
    CHECK_STR(out, "/c");
    CHECK_INT(vpath_resolve("/d", "C:/Windows/System32", out, sizeof out), 0);
    CHECK_STR(out, "/c/Windows/System32");

    // "/c" and "/" are what cd needs most; both used to be fragile
    CHECK_INT(vpath_resolve("/c/Users/me", "/c", out, sizeof out), 0);
    CHECK_STR(out, "/c");

    // not in the tree
    errno = 0;
    CHECK_INT(vpath_resolve("/c", "\\\\server\\share", out, sizeof out), -1);
    CHECK_INT(errno, ENOENT);
    CHECK_INT(vpath_resolve("/c", "\\rooted", out, sizeof out), -1);

    // a buffer that is too short is an error, not an overflow
    {
        char tiny[8];
        errno = 0;
        CHECK_INT(vpath_resolve("/c/Users", "a/long/name", tiny, sizeof tiny), -1);
        CHECK_INT(errno, ENAMETOOLONG);
    }
}

static void test_absolute_to_native(void) {
    char out[VPATH_MAX];

    printf("vpath_absolute_to_native\n");
    CHECK_INT(vpath_absolute_to_native("/", out, sizeof out), VPATH_ROOT);
    CHECK_STR(out, "");

    // the root of a drive; this was read past the end of the string once
    CHECK_INT(vpath_absolute_to_native("/c", out, sizeof out), 0);
    CHECK_STR(out, "C:\\");
    CHECK_INT(vpath_absolute_to_native("/d", out, sizeof out), 0);
    CHECK_STR(out, "D:\\");

    // exactly enough room for "C:\" and its terminator
    {
        char tight[4];
        memset(tight, 'x', sizeof tight);
        CHECK_INT(vpath_absolute_to_native("/c", tight, sizeof tight), 0);
        CHECK_STR(tight, "C:\\");
    }

    CHECK_INT(vpath_absolute_to_native("/c/Users/me", out, sizeof out), 0);
    CHECK_STR(out, "C:\\Users\\me");

    CHECK_INT(vpath_absolute_to_native("/c/a", out, sizeof out), 0);
    CHECK_STR(out, "C:\\a");

    // not drives
    errno = 0;
    CHECK_INT(vpath_absolute_to_native("/cc", out, sizeof out), -1);
    CHECK_INT(errno, ENOENT);
    CHECK_INT(vpath_absolute_to_native("/usr/bin", out, sizeof out), -1);
    CHECK_INT(vpath_absolute_to_native("/1", out, sizeof out), -1);
    CHECK_INT(vpath_absolute_to_native("/c:", out, sizeof out), -1);

    errno = 0;
    {
        char tiny[4];
        CHECK_INT(vpath_absolute_to_native("/c/Users", tiny, sizeof tiny), -1);
        CHECK_INT(errno, ENAMETOOLONG);
    }
}

static void test_root_native(void) {
    char out[8];

    printf("vpath_root_native\n");
    CHECK_INT(vpath_root_native(out, sizeof out), 0);
    CHECK_STR(out, driveRoot);
}

static void test_drives(void) {
    char letters[27];
    size_t count, i;
    int found = 0;
    char systemDrive = (char)((windowsDir[0] >= 'A' && windowsDir[0] <= 'Z') ? windowsDir[0] + 32 : windowsDir[0]);

    printf("vpath_drives\n");
    count = vpath_drives(letters);
    CHECK(count >= 1);
    CHECK_INT(strlen(letters), count);

    // lower case letters in alphabetical order; the system drive is among them
    for (i = 0; i < count; i++) {
        CHECK(letters[i] >= 'a' && letters[i] <= 'z');
        if (i > 0)
            CHECK(letters[i] > letters[i - 1]);
        if (letters[i] == systemDrive)
            found = 1;
    }
    CHECK(found);
}

static void test_getcwd(void) {
    char out[VPATH_MAX];

    printf("vpath_getcwd\n");

    // without PWD the directory of Windows decides
    SetCurrentDirectoryA(windowsDir);
    _putenv_s("PWD", "");
    CHECK_INT(vpath_getcwd(out, sizeof out), 0);
    CHECK_STR(out, virtualWindowsDir);

    // PWD that matches is used (and normalized)
    {
        char value[VPATH_MAX];
        snprintf(value, sizeof value, "%s/./", virtualWindowsDir);
        _putenv_s("PWD", value);
        CHECK_INT(vpath_getcwd(out, sizeof out), 0);
        CHECK_STR(out, virtualWindowsDir);
    }

    // PWD that no longer matches the directory is ignored
    _putenv_s("PWD", "/c/some/other/place");
    CHECK_INT(vpath_getcwd(out, sizeof out), 0);
    CHECK_STR(out, virtualWindowsDir);

    // "/" is believed only while the process sits on the root of the system drive
    _putenv_s("PWD", "/");
    CHECK_INT(vpath_getcwd(out, sizeof out), 0);
    CHECK_STR(out, virtualWindowsDir);

    SetCurrentDirectoryA(driveRoot);
    CHECK_INT(vpath_getcwd(out, sizeof out), 0);
    CHECK_STR(out, "/");

    _putenv_s("PWD", "");
    CHECK_INT(vpath_getcwd(out, sizeof out), 0);
    {
        char drive[4] = { (char)((driveRoot[0] >= 'A' && driveRoot[0] <= 'Z') ? driveRoot[0] + 32 : driveRoot[0]), '\0' };
        char expected[8];
        snprintf(expected, sizeof expected, "/%s", drive);
        CHECK_STR(out, expected);
    }
}

static void test_to_native(void) {
    char out[VPATH_MAX], expected[VPATH_MAX];

    printf("vpath_to_native\n");

    // absolute virtual paths do not depend on the current directory
    SetCurrentDirectoryA(windowsDir);
    _putenv_s("PWD", virtualWindowsDir);

    CHECK_INT(vpath_to_native("/c/Users/me", out, sizeof out), 0);
    CHECK_STR(out, "C:\\Users\\me");
    CHECK_INT(vpath_to_native("/", out, sizeof out), VPATH_ROOT);

    // Windows notation is passed through as it is
    CHECK_INT(vpath_to_native("C:\\Users\\me", out, sizeof out), 0);
    CHECK_STR(out, "C:\\Users\\me");
    CHECK_INT(vpath_to_native("\\\\server\\share\\file", out, sizeof out), 0);
    CHECK_STR(out, "\\\\server\\share\\file");
    CHECK_INT(vpath_to_native("\\rooted", out, sizeof out), 0);
    CHECK_STR(out, "\\rooted");

    // relative paths start at the current directory
    snprintf(expected, sizeof expected, "%s\\System32", windowsDir);
    CHECK_INT(vpath_to_native("System32", out, sizeof out), 0);
    CHECK_STR(out, expected);
    CHECK_INT(vpath_to_native("./System32", out, sizeof out), 0);
    CHECK_STR(out, expected);

    // ".." climbs through the virtual tree, up to the root and no further
    CHECK_INT(vpath_to_native("..", out, sizeof out), 0);
    CHECK_STR(out, driveRoot);
    CHECK_INT(vpath_to_native("../..", out, sizeof out), VPATH_ROOT);
    CHECK_INT(vpath_to_native("../../../nodrive", out, sizeof out), -1);
    // "x" is a drive name like any other letter
    CHECK_INT(vpath_to_native("../../../x", out, sizeof out), 0);
    CHECK_STR(out, "X:\\");

    // in the root, relative paths only know the drives
    SetCurrentDirectoryA(driveRoot);
    _putenv_s("PWD", "/");
    errno = 0;
    CHECK_INT(vpath_to_native("Windows", out, sizeof out), -1);
    CHECK_INT(errno, ENOENT);
    CHECK_INT(vpath_to_native("c", out, sizeof out), 0);
    CHECK_STR(out, driveRoot);
    CHECK_INT(vpath_to_native(".", out, sizeof out), VPATH_ROOT);
    CHECK_INT(vpath_to_native("..", out, sizeof out), VPATH_ROOT);

    errno = 0;
    {
        char tiny[4];
        CHECK_INT(vpath_to_native("/c/Users/me", tiny, sizeof tiny), -1);
        CHECK_INT(errno, ENAMETOOLONG);
    }
}

int main(void) {
    char original[MAX_PATH];

    GetCurrentDirectoryA(sizeof original, original);
    GetWindowsDirectoryA(windowsDir, sizeof windowsDir);
    snprintf(driveRoot, sizeof driveRoot, "%c:\\", windowsDir[0]);
    expected_virtual(windowsDir, virtualWindowsDir, sizeof virtualWindowsDir);

    test_from_native();
    test_resolve();
    test_absolute_to_native();
    test_root_native();
    test_drives();
    test_getcwd();
    test_to_native();

    SetCurrentDirectoryA(original);
    return test_summary("vpath_test");
}
