/*
 * Tests of the commands: arch, echo, pwd and cat, run as the programs they
 * are, with their arguments, standard input and exit codes.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include <errno.h>
#include "testlib.h"

static char workDir[MAX_PATH];
static char virtualWorkDir[MAX_PATH];

/** Runs bin\<name>.exe; the output is only valid until run_result_free. */
static int run_command(const char* name, const char* arguments, const char* input, RunResult* result) {
    char path[MAX_PATH * 2];
    int status;

    bin_path(name, path, sizeof path);
    status = run_program(path, arguments, input, workDir, result);
    CHECK(status == 0);
    CHECK(!result->timedOut);
    return status;
}

static void test_echo(void) {
    RunResult r;

    printf("echo\n");

    run_command("echo", "hello world", NULL, &r);
    CHECK_STR(lf_only(r.out), "hello world\n");
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    // the words are joined by one space whatever separated them
    run_command("echo", "a    b", NULL, &r);
    CHECK_STR(lf_only(r.out), "a b\n");
    run_result_free(&r);

    run_command("echo", "", NULL, &r);
    CHECK_STR(lf_only(r.out), "\n");
    run_result_free(&r);

    run_command("echo", "-n no newline", NULL, &r);
    CHECK_STR(lf_only(r.out), "no newline");
    run_result_free(&r);

    // escapes: only with -e
    run_command("echo", "\"a\\tb\"", NULL, &r);
    CHECK_STR(lf_only(r.out), "a\\tb\n");
    run_result_free(&r);

    run_command("echo", "-e \"a\\tb\\\\c\\x41\\0102\"", NULL, &r);
    CHECK_STR(lf_only(r.out), "a\tb\\cAB\n");
    run_result_free(&r);

    run_command("echo", "-e \"stop\\cnot shown\"", NULL, &r);
    CHECK_STR(lf_only(r.out), "stop");
    run_result_free(&r);

    // -E switches escapes off again, options may be grouped
    run_command("echo", "-eE \"a\\tb\"", NULL, &r);
    CHECK_STR(lf_only(r.out), "a\\tb\n");
    run_result_free(&r);

    run_command("echo", "-ne \"a\\nb\"", NULL, &r);
    CHECK_STR(lf_only(r.out), "a\nb");
    run_result_free(&r);

    // not options: printed as they are
    run_command("echo", "-x -n -", NULL, &r);
    CHECK_STR(lf_only(r.out), "-x -n -\n");
    run_result_free(&r);

    run_command("echo", "--version", NULL, &r);
    CHECK(strstr(r.out, "echo") != NULL);
    run_result_free(&r);

    run_command("echo", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: echo") != NULL);
    run_result_free(&r);
}

static void test_arch(void) {
    RunResult r;
    const char* known[] = { "x86_64\n", "i686\n", "aarch64\n", "armv7l\n", "ia64\n", "mips\n",
                            "alpha\n", "powerpc\n", "sh4\n", "anycpu\n", "unknown\n" };
    size_t i;
    int matches = 0;

    printf("arch\n");
    run_command("arch", "", NULL, &r);
    lf_only(r.out);
    for (i = 0; i < sizeof known / sizeof known[0]; i++)
        if (strcmp(r.out, known[i]) == 0)
            matches++;
    CHECK_INT(matches, 1);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
}

static void test_pwd(void) {
    RunResult r;
    char expected[MAX_PATH * 2];

    printf("pwd\n");

    // the directory it runs in, spelled as a virtual path
    run_command("pwd", "-P", NULL, &r);
    snprintf(expected, sizeof expected, "%s\n", virtualWorkDir);
    CHECK_STR(lf_only(r.out), expected);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_command("pwd", "", NULL, &r);
    CHECK_STR(lf_only(r.out), expected);
    run_result_free(&r);

    run_command("pwd", "--bogus", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "invalid option") != NULL);
    run_result_free(&r);

    run_command("pwd", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: pwd") != NULL);
    run_result_free(&r);
}

static void write_fixtures(void) {
    static const unsigned char allBytes[256] = {
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
        32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
        64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,
        96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,
        121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,
        145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,
        169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,
        193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,
        217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,
        241,242,243,244,245,246,247,248,249,250,251,252,253,254,255 };

    write_file(workDir, "all.bin", allBytes, sizeof allBytes);
    write_text(workDir, "lf.txt", "a\n\n\n\tb\nc");              // no newline at the end
    write_text(workDir, "crlf.txt", "a\r\n\r\n\r\n\tb\r\nc\r\n");
    write_text(workDir, "cr.txt", "\rx\r");                      // lone CRs
    write_text(workDir, "two.txt", "two\n");
    write_text(workDir, "high.txt", "\x01\x7f\xc3\xa9\xff");     // control, DEL, and bytes >= 128
}

/** Runs cat with arguments and compares the exact bytes written. */
static void cat_expect(const char* arguments, const char* input, const char* expected, size_t expectedLength) {
    RunResult r;
    run_command("cat", arguments, input, &r);
    CHECK(r.out != NULL && strlen(r.out) == expectedLength && memcmp(r.out, expected, expectedLength) == 0);
    if (r.out == NULL || strlen(r.out) != expectedLength || memcmp(r.out, expected, expectedLength) != 0)
        printf("    for: cat %s\n    got: \"%s\"\n", arguments, r.out == NULL ? "(null)" : r.out);
    run_result_free(&r);
}

#define CAT_EXPECT(arguments, input, expected) cat_expect(arguments, input, expected, sizeof expected - 1)

static void test_cat(void) {
    RunResult r;
    size_t length;
    char* original;

    printf("cat\n");
    write_fixtures();

    // bytes pass through unchanged: every value, and CR LF as it is
    run_command("cat", "all.bin", NULL, &r);
    original = read_file(workDir, "all.bin", &length);
    CHECK_INT(length, 256);
    CHECK(original != NULL && r.out != NULL && memcmp(original, r.out, 256) == 0);
    free(original);
    run_result_free(&r);

    CAT_EXPECT("crlf.txt", NULL, "a\r\n\r\n\r\n\tb\r\nc\r\n");
    CAT_EXPECT("lf.txt two.txt", NULL, "a\n\n\n\tb\nctwo\n");

    // standard input, and "-" in the middle
    CAT_EXPECT("", "from stdin\r\n", "from stdin\r\n");
    CAT_EXPECT("lf.txt - two.txt", "<stdin>", "a\n\n\n\tb\nc<stdin>two\n");

    // numbering
    CAT_EXPECT("-n lf.txt", NULL, "     1\ta\n     2\t\n     3\t\n     4\t\tb\n     5\tc");
    CAT_EXPECT("-b lf.txt", NULL, "     1\ta\n\n\n     2\t\tb\n     3\tc");
    CAT_EXPECT("-b -n lf.txt", NULL, "     1\ta\n\n\n     2\t\tb\n     3\tc");    // -b wins
    CAT_EXPECT("-s lf.txt", NULL, "a\n\n\tb\nc");
    CAT_EXPECT("-ns lf.txt", NULL, "     1\ta\n     2\t\n     3\t\tb\n     4\tc");

    // numbers continue over files, like one stream
    CAT_EXPECT("-n two.txt two.txt", NULL, "     1\ttwo\n     2\ttwo\n");

    // visible ends and tabs
    CAT_EXPECT("-E lf.txt", NULL, "a$\n$\n$\n\tb$\nc");
    CAT_EXPECT("-T lf.txt", NULL, "a\n\n\n^Ib\nc");
    CAT_EXPECT("-A lf.txt", NULL, "a$\n$\n$\n^Ib$\nc");
    CAT_EXPECT("-e lf.txt", NULL, "a$\n$\n$\n\tb$\nc");
    CAT_EXPECT("-t lf.txt", NULL, "a\n\n\n^Ib\nc");

    // non printing characters
    CAT_EXPECT("-v high.txt", NULL, "^A^?M-CM-)M-^?");

    // CR LF is one line ending: it counts as blank, and $ goes in front of it
    CAT_EXPECT("-b crlf.txt", NULL, "     1\ta\r\n\r\n\r\n     2\t\tb\r\n     3\tc\r\n");
    CAT_EXPECT("-s crlf.txt", NULL, "a\r\n\r\n\tb\r\nc\r\n");
    CAT_EXPECT("-E crlf.txt", NULL, "a$\r\n$\r\n$\r\n\tb$\r\nc$\r\n");
    // ... and with -v the CR is shown as GNU cat does
    CAT_EXPECT("-A crlf.txt", NULL, "a^M$\n^M$\n^M$\n^Ib^M$\nc^M$\n");
    CAT_EXPECT("-v crlf.txt", NULL, "a^M\n^M\n^M\n\tb^M\nc^M\n");

    // CRs that are not part of a CR LF are ordinary characters
    CAT_EXPECT("-A cr.txt", NULL, "^Mx^M");
    CAT_EXPECT("-E cr.txt", NULL, "\rx\r");

    // errors: the other files are still shown, the exit code tells
    run_command("cat", "nosuch.txt two.txt", NULL, &r);
    CHECK_STR(lf_only(r.out), "two\n");
    CHECK(strstr(r.err, "cat: nosuch.txt: No such file or directory") != NULL);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    run_command("cat", "/", NULL, &r);
    CHECK(strstr(r.err, "cat: /: Is a directory") != NULL);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    // a directory of the work directory, in virtual and in Windows notation
    {
        char arguments[MAX_PATH * 2];
        snprintf(arguments, sizeof arguments, "\"%s\"", workDir);
        run_command("cat", arguments, NULL, &r);
        CHECK(strstr(r.err, "Is a directory") != NULL);
        CHECK_INT(r.exitCode, 1);
        run_result_free(&r);

        snprintf(arguments, sizeof arguments, "\"%s/two.txt\"", virtualWorkDir);
        run_command("cat", arguments, NULL, &r);
        CHECK_STR(lf_only(r.out), "two\n");
        CHECK_INT(r.exitCode, 0);
        run_result_free(&r);

        snprintf(arguments, sizeof arguments, "\"%s\\two.txt\"", workDir);
        run_command("cat", arguments, NULL, &r);
        CHECK_STR(lf_only(r.out), "two\n");
        run_result_free(&r);
    }

    run_command("cat", "-x", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "invalid option -- 'x'") != NULL);
    run_result_free(&r);

    run_command("cat", "--bogus", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "unrecognized option '--bogus'") != NULL);
    run_result_free(&r);

    // "--" ends the options
    CAT_EXPECT("-- two.txt", NULL, "two\n");

    run_command("cat", "--version", NULL, &r);
    CHECK(strstr(r.out, "cat") != NULL);
    run_result_free(&r);

    run_command("cat", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: cat") != NULL);
    run_result_free(&r);
}

/** Writes a file of the given size, clearing attributes an earlier run may have set. */
static void make_sized_file(const char* directory, const char* name, size_t size, DWORD attributes) {
    char path[MAX_PATH * 2];
    char* data = malloc(size + 1);

    snprintf(path, sizeof path, "%s\\%s", directory, name);
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    memset(data, 'x', size);
    write_file(directory, name, data, size);
    if (attributes != FILE_ATTRIBUTE_NORMAL)
        SetFileAttributesA(path, attributes);
    free(data);
}

static void set_mtime(const char* directory, const char* name, int year, int month, int day) {
    char path[MAX_PATH * 2];
    SYSTEMTIME t;
    FILETIME ft;
    HANDLE h;

    snprintf(path, sizeof path, "%s\\%s", directory, name);
    memset(&t, 0, sizeof t);
    t.wYear = (WORD)year;
    t.wMonth = (WORD)month;
    t.wDay = (WORD)day;
    t.wHour = 12;
    SystemTimeToFileTime(&t, &ft);
    h = CreateFileA(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SetFileTime(h, NULL, NULL, &ft);
        CloseHandle(h);
    }
}

/** Runs ls with arguments in the work directory and compares the output, which is LF only. */
static void ls_expect(const char* arguments, const char* expected) {
    RunResult r;
    run_command("ls", arguments, NULL, &r);
    CHECK_STR(r.out, expected);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
}

static int has_line(const char* text, const char* line) {
    size_t length = strlen(line);
    const char* p = text;
    while ((p = strstr(p, line)) != NULL) {
        if ((p == text || p[-1] == '\n') && p[length] == '\n')
            return 1;
        p += length;
    }
    return 0;
}

static void test_ls(void) {
    RunResult r;
    char dir[MAX_PATH * 2], line[MAX_PATH * 3], windowsDir[MAX_PATH], drive[4];

    printf("ls\n");

    snprintf(dir, sizeof dir, "%s\\lsdir", workDir);
    CreateDirectoryA(dir, NULL);
    snprintf(line, sizeof line, "%s\\lsdir\\sub", workDir);
    CreateDirectoryA(line, NULL);
    make_sized_file(dir, "a.txt", 3, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(dir, "B.txt", 10, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(dir, "big.bin", 1500, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(dir, "ro.txt", 1, FILE_ATTRIBUTE_READONLY);
    make_sized_file(dir, "Zed.exe", 100, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(dir, ".dotfile", 1, FILE_ATTRIBUTE_NORMAL);
    make_sized_file(dir, "hid.dat", 1, FILE_ATTRIBUTE_HIDDEN);
    snprintf(line, sizeof line, "%s\\sub", dir);
    make_sized_file(line, "inner.txt", 5, FILE_ATTRIBUTE_NORMAL);

    // names are sorted without regard to case; hidden ones are not shown
    ls_expect("lsdir", "a.txt\nB.txt\nbig.bin\nro.txt\nsub\nZed.exe\n");
    ls_expect("-1 lsdir", "a.txt\nB.txt\nbig.bin\nro.txt\nsub\nZed.exe\n");
    ls_expect("-r lsdir", "Zed.exe\nsub\nro.txt\nbig.bin\nB.txt\na.txt\n");

    // dot files and files with the hidden attribute
    ls_expect("-a lsdir", ".\n..\n.dotfile\na.txt\nB.txt\nbig.bin\nhid.dat\nro.txt\nsub\nZed.exe\n");
    ls_expect("-A lsdir", ".dotfile\na.txt\nB.txt\nbig.bin\nhid.dat\nro.txt\nsub\nZed.exe\n");

    // sorting by size, largest first
    ls_expect("-S lsdir", "big.bin\nZed.exe\nB.txt\na.txt\nro.txt\nsub\n");

    // indicators
    ls_expect("-F lsdir", "a.txt\nB.txt\nbig.bin\nro.txt\nsub/\nZed.exe*\n");
    ls_expect("-p lsdir", "a.txt\nB.txt\nbig.bin\nro.txt\nsub/\nZed.exe\n");

    // directories as themselves, and files given by name
    ls_expect("-d lsdir", "lsdir\n");
    ls_expect("lsdir/a.txt", "lsdir/a.txt\n");
    ls_expect("lsdir/sub", "inner.txt\n");

    // several operands: files first, then every directory with a header
    ls_expect("lsdir/a.txt lsdir/sub", "lsdir/a.txt\n\nlsdir/sub:\ninner.txt\n");

    // recursion
    ls_expect("-R lsdir", "lsdir:\na.txt\nB.txt\nbig.bin\nro.txt\nsub\nZed.exe\n\nlsdir/sub:\ninner.txt\n");

    // virtual paths
    snprintf(line, sizeof line, "%s/lsdir/sub", virtualWorkDir);
    ls_expect(line, "inner.txt\n");

    // the long format
    run_command("ls", "-l lsdir", NULL, &r);
    CHECK(strstr(r.out, "total ") == r.out);
    CHECK(strstr(r.out, "\n-rw-r--r-- 1 ") != NULL);
    CHECK(strstr(r.out, "\n-r--r--r-- 1 ") != NULL);           // read-only
    CHECK(strstr(r.out, "\ndrwxr-xr-x 1 ") != NULL);           // directory
    CHECK(strstr(r.out, "\n-rwxr-xr-x 1 ") != NULL);           // program
    CHECK(strstr(r.out, " a.txt\n") != NULL && strstr(r.out, " Zed.exe\n") != NULL);
    CHECK(strstr(r.out, "hid.dat") == NULL && strstr(r.out, ".dotfile") == NULL);
    // sizes are right aligned to the widest one
    CHECK(strstr(r.out, " 1500 ") != NULL);
    CHECK(strstr(r.out, "    3 ") != NULL);
    run_result_free(&r);

    run_command("ls", "-lh lsdir", NULL, &r);
    CHECK(strstr(r.out, " 1.5K ") != NULL);
    CHECK(strstr(r.out, " 100 ") != NULL);
    run_result_free(&r);

    // modification times: -t newest first, and the year for an old file
    set_mtime(dir, "a.txt", 2001, 6, 15);
    set_mtime(dir, "B.txt", 2005, 6, 15);
    set_mtime(dir, "big.bin", 2003, 6, 15);
    set_mtime(dir, "ro.txt", 2002, 6, 15);
    set_mtime(dir, "Zed.exe", 2004, 6, 15);
    ls_expect("-t lsdir", "sub\nB.txt\nZed.exe\nbig.bin\nro.txt\na.txt\n");
    ls_expect("-tr lsdir", "a.txt\nro.txt\nbig.bin\nZed.exe\nB.txt\nsub\n");
    run_command("ls", "-l lsdir/a.txt", NULL, &r);
    CHECK(strstr(r.out, " Jun 15  2001 lsdir/a.txt\n") != NULL);
    run_result_free(&r);

    // the virtual root holds the drives
    GetWindowsDirectoryA(windowsDir, sizeof windowsDir);
    drive[0] = (char)((windowsDir[0] >= 'A' && windowsDir[0] <= 'Z') ? windowsDir[0] + 32 : windowsDir[0]);
    drive[1] = '\0';
    run_command("ls", "/", NULL, &r);
    CHECK(has_line(r.out, drive));
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_command("ls", "-a /", NULL, &r);
    CHECK(strstr(r.out, ".\n..\n") == r.out);
    run_result_free(&r);

    snprintf(line, sizeof line, "/%s", drive);
    run_command("ls", line, NULL, &r);
    CHECK(has_line(r.out, "Windows") || has_line(r.out, "windows") || has_line(r.out, "WINDOWS"));
    run_result_free(&r);

    // errors: nothing to access is an exit code of 2
    run_command("ls", "nosuch lsdir/a.txt", NULL, &r);
    CHECK(strstr(r.err, "ls: cannot access 'nosuch': No such file or directory") != NULL);
    CHECK_STR(lf_only(r.out), "lsdir/a.txt\n");
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);

    run_command("ls", "-x", NULL, &r);
    CHECK(strstr(r.err, "invalid option -- 'x'") != NULL);
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);

    run_command("ls", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: ls") != NULL);
    run_result_free(&r);

    run_command("ls", "--version", NULL, &r);
    CHECK(strstr(r.out, "ls (Winshell)") != NULL);
    run_result_free(&r);

    // the ignored colour option
    ls_expect("--color=auto lsdir/sub", "inner.txt\n");
}

/* ---- rm, cp, mv ------------------------------------------------------------- */

/** Runs bin\<name>.exe in the given directory. */
static void run_tool(const char* cwd, const char* name, const char* arguments, const char* input, RunResult* r) {
    char path[MAX_PATH * 2];
    bin_path(name, path, sizeof path);
    CHECK(run_program(path, arguments, input, cwd, r) == 0);
    CHECK(!r->timedOut);
}

/** Makes the directory empty (and read-write) again; done with the system tools, not with those under test. */
static void reset_dir(const char* path) {
    char arguments[MAX_PATH * 4];
    const char* comspec = getenv("ComSpec");
    RunResult r;

    snprintf(arguments, sizeof arguments,
             "/c attrib -r \"%s\\*\" /s /d >nul 2>&1 & rd /s /q \"%s\" >nul 2>&1 & md \"%s\"", path, path, path);
    run_program(comspec != NULL ? comspec : "cmd.exe", arguments, NULL, NULL, &r);
    run_result_free(&r);
}

static int path_exists_in(const char* directory, const char* name) {
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\%s", directory, name);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static DWORD attributes_in(const char* directory, const char* name) {
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\%s", directory, name);
    return GetFileAttributesA(path);
}

static void make_dir_in(const char* directory, const char* name) {
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\%s", directory, name);
    CreateDirectoryA(path, NULL);
}

static int year_of(const char* directory, const char* name) {
    char path[MAX_PATH * 2];
    HANDLE h;
    FILETIME ft;
    SYSTEMTIME t;
    int year = 0;

    snprintf(path, sizeof path, "%s\\%s", directory, name);
    h = CreateFileA(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        if (GetFileTime(h, NULL, NULL, &ft) && FileTimeToSystemTime(&ft, &t))
            year = t.wYear;
        CloseHandle(h);
    }
    return year;
}

/** The content of a file must be exactly the given text. */
static void expect_content(const char* directory, const char* name, const char* expected) {
    char* text = read_file(directory, name, NULL);
    if (text == NULL) {
        g_checks++;
        g_failures++;
        printf("  FAIL missing file %s\\%s\n", directory, name);
        return;
    }
    CHECK_STR(text, expected);
    free(text);
}

static void test_rm(void) {
    RunResult r;
    char d[MAX_PATH * 2];

    printf("rm\n");
    snprintf(d, sizeof d, "%s\\rm", workDir);
    reset_dir(d);
    write_text(d, "f1.txt", "x");
    write_text(d, "f2.txt", "x");

    run_tool(d, "rm", "f1.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "f1.txt"));
    run_result_free(&r);

    run_tool(d, "rm", "f1.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rm: cannot remove 'f1.txt': No such file or directory") != NULL);
    run_result_free(&r);

    run_tool(d, "rm", "-f f1.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK_STR(r.err, "");
    run_result_free(&r);

    run_tool(d, "rm", "-v f2.txt", NULL, &r);
    CHECK_STR(lf_only(r.out), "removed 'f2.txt'\n");
    CHECK(!path_exists_in(d, "f2.txt"));
    run_result_free(&r);

    // directories need -r (or -d, when they are empty)
    make_dir_in(d, "sub");
    write_text(d, "sub\\x.txt", "x");
    run_tool(d, "rm", "sub", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rm: cannot remove 'sub': Is a directory") != NULL);
    CHECK(path_exists_in(d, "sub\\x.txt"));
    run_result_free(&r);

    run_tool(d, "rm", "-r sub", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "sub"));
    run_result_free(&r);

    make_dir_in(d, "empty");
    run_tool(d, "rm", "-d empty", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "empty"));
    run_result_free(&r);

    make_dir_in(d, "full");
    write_text(d, "full\\y.txt", "y");
    run_tool(d, "rm", "-d full", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "Directory not empty") != NULL);
    run_result_free(&r);

    // a read-only file goes like any other file
    make_sized_file(d, "ro.txt", 1, FILE_ATTRIBUTE_READONLY);
    run_tool(d, "rm", "ro.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "ro.txt"));
    run_result_free(&r);

    // a tree with several levels, read-only files inside, verbose
    make_dir_in(d, "deep");
    make_dir_in(d, "deep\\a");
    make_dir_in(d, "deep\\a\\b");
    write_text(d, "deep\\a\\b\\f.txt", "1");
    make_sized_file(d, "deep\\a\\ro.txt", 1, FILE_ATTRIBUTE_READONLY);
    run_tool(d, "rm", "-rv deep", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "deep"));
    CHECK(strstr(r.out, "removed directory 'deep'") != NULL);
    CHECK(strstr(r.out, "removed 'deep/a/ro.txt'") != NULL);
    run_result_free(&r);

    // -i asks, and the answer decides
    write_text(d, "q.txt", "q");
    run_tool(d, "rm", "-i q.txt", "n\n", &r);
    CHECK(path_exists_in(d, "q.txt"));
    CHECK(strstr(r.err, "rm: remove regular file 'q.txt'?") != NULL);
    run_result_free(&r);
    run_tool(d, "rm", "-i q.txt", "y\n", &r);
    CHECK(!path_exists_in(d, "q.txt"));
    run_result_free(&r);

    // the last of -i and -f wins
    write_text(d, "w.txt", "w");
    run_tool(d, "rm", "-i -f w.txt", "n\n", &r);
    CHECK(!path_exists_in(d, "w.txt"));
    run_result_free(&r);

    // things that are never removed
    run_tool(d, "rm", ".", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "refusing to remove '.' or '..'") != NULL);
    run_result_free(&r);

    // The roots of file systems, in every spelling. These run with -i and the answer "n":
    // should the protection ever fail, nothing is removed, and the test notices the question.
    {
        char drive = virtualWorkDir[1];
        char upper = (char)((drive >= 'a' && drive <= 'z') ? drive - 32 : drive);
        char arguments[64];
        size_t k;
        const char* formats[] = {
            "-ri /",                        // the virtual root
            "-ri /%c",                      // a drive
            "-ri /%c/",
            "-ri %C:\\",                    // Windows notation
            "-ri %C:/",
            "-ri %C:",
            "-ri \\",                       // the root of the current drive
            "-ri \\\\?\\%C:\\",             // the long path prefix
            "-ri --no-preserve-root /%c",   // is not lifted by the option
            "-rfi /%c",
            "-di /%c",
        };

        for (k = 0; k < sizeof formats / sizeof formats[0]; k++) {
            // %c is the lower case drive, %C the upper case one
            char* out = arguments;
            const char* f = formats[k];
            for (; *f != '\0' && out < arguments + sizeof arguments - 2; f++) {
                if (f[0] == '%' && f[1] == 'c') {
                    *out++ = drive;
                    f++;
                } else if (f[0] == '%' && f[1] == 'C') {
                    *out++ = upper;
                    f++;
                } else {
                    *out++ = *f;
                }
            }
            *out = '\0';

            run_tool(d, "rm", arguments, "n\nn\nn\nn\n", &r);
            if (r.exitCode != 1 || strstr(r.err, "dangerous to operate recursively") == NULL ||
                strstr(r.err, "descend into") != NULL || strstr(r.err, "remove directory") != NULL)
                printf("    for: rm %s\n    err: %s\n", arguments, r.err);
            CHECK_INT(r.exitCode, 1);
            CHECK(strstr(r.err, "dangerous to operate recursively") != NULL);
            CHECK(strstr(r.err, "descend into") == NULL);
            run_result_free(&r);
        }
    }

    // a root reached by ".." is refused as ".." before it is even looked at
    {
        char arguments[64];
        snprintf(arguments, sizeof arguments, "-ri /%c/Users/..", virtualWorkDir[1]);
        run_tool(d, "rm", arguments, "n\nn\n", &r);
        CHECK_INT(r.exitCode, 1);
        CHECK(strstr(r.err, "refusing to remove '.' or '..'") != NULL);
        CHECK(strstr(r.err, "descend into") == NULL);
        run_result_free(&r);
    }

    // without -r a root is just a directory
    run_tool(d, "rm", "/", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rm: cannot remove '/': Is a directory") != NULL);
    run_result_free(&r);

    run_tool(d, "rm", "", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rm: missing operand") != NULL);
    run_result_free(&r);

    run_tool(d, "rm", "-f", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_tool(d, "rm", "-rf nosuch", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_tool(d, "rm", "-x", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    run_tool(d, "rm", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: rm") != NULL);
    run_result_free(&r);
}

static void test_cp(void) {
    RunResult r;
    char d[MAX_PATH * 2];

    printf("cp\n");
    snprintf(d, sizeof d, "%s\\cp", workDir);
    reset_dir(d);
    write_text(d, "src.txt", "hello");

    run_tool(d, "cp", "src.txt dst.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "dst.txt", "hello");
    expect_content(d, "src.txt", "hello");
    run_result_free(&r);

    run_tool(d, "cp", "-v src.txt dst2.txt", NULL, &r);
    CHECK_STR(lf_only(r.out), "'src.txt' -> 'dst2.txt'\n");
    run_result_free(&r);

    // all byte values, and a file larger than the copy buffer
    {
        unsigned char* big = malloc(200000);
        size_t i;
        for (i = 0; i < 200000; i++)
            big[i] = (unsigned char)(i * 7 + i / 256);
        write_file(d, "big.bin", big, 200000);
        run_tool(d, "cp", "big.bin big2.bin", NULL, &r);
        CHECK_INT(r.exitCode, 0);
        {
            size_t length = 0;
            char* copy = read_file(d, "big2.bin", &length);
            CHECK_INT(length, 200000);
            CHECK(copy != NULL && memcmp(copy, big, 200000) == 0);
            free(copy);
        }
        run_result_free(&r);
        free(big);
    }

    // an existing file is replaced; -n and -i
    write_text(d, "dst.txt", "old");
    run_tool(d, "cp", "src.txt dst.txt", NULL, &r);
    expect_content(d, "dst.txt", "hello");
    run_result_free(&r);

    write_text(d, "keep.txt", "keep");
    run_tool(d, "cp", "-n src.txt keep.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "keep.txt", "keep");
    run_result_free(&r);

    run_tool(d, "cp", "-i src.txt keep.txt", "n\n", &r);
    expect_content(d, "keep.txt", "keep");
    CHECK(strstr(r.err, "cp: overwrite 'keep.txt'?") != NULL);
    run_result_free(&r);

    run_tool(d, "cp", "-i src.txt keep.txt", "y\n", &r);
    expect_content(d, "keep.txt", "hello");
    run_result_free(&r);

    // errors
    run_tool(d, "cp", "src.txt src.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "'src.txt' and 'src.txt' are the same file") != NULL);
    run_result_free(&r);

    run_tool(d, "cp", "nosuch.txt x.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cp: cannot stat 'nosuch.txt': No such file or directory") != NULL);
    run_result_free(&r);

    run_tool(d, "cp", "src.txt nodir\\x.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot create regular file") != NULL);
    run_result_free(&r);

    run_tool(d, "cp", "", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cp: missing file operand") != NULL);
    run_result_free(&r);

    run_tool(d, "cp", "src.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "missing destination file operand after 'src.txt'") != NULL);
    run_result_free(&r);

    // directories
    make_dir_in(d, "tree");
    make_dir_in(d, "tree\\sub");
    write_text(d, "tree\\a.txt", "A");
    write_text(d, "tree\\sub\\b.txt", "B");

    run_tool(d, "cp", "tree out", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cp: -r not specified; omitting directory 'tree'") != NULL);
    CHECK(!path_exists_in(d, "out"));
    run_result_free(&r);

    run_tool(d, "cp", "-r tree out", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "out\\a.txt", "A");
    expect_content(d, "out\\sub\\b.txt", "B");
    run_result_free(&r);

    // onto an existing directory: into it
    run_tool(d, "cp", "-r tree out", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "out\\tree\\sub\\b.txt", "B");
    run_result_free(&r);

    run_tool(d, "cp", "-r tree tree\\sub", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot copy a directory, 'tree', into itself") != NULL);
    run_result_free(&r);

    run_tool(d, "cp", "-r tree src.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot overwrite non-directory 'src.txt' with directory 'tree'") != NULL);
    run_result_free(&r);

    // several sources, and the target directory option
    make_dir_in(d, "into");
    write_text(d, "s2.txt", "two");
    run_tool(d, "cp", "src.txt s2.txt into", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "into\\src.txt", "hello");
    expect_content(d, "into\\s2.txt", "two");
    run_result_free(&r);

    make_dir_in(d, "into2");
    run_tool(d, "cp", "-t into2 src.txt s2.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "into2\\s2.txt", "two");
    run_result_free(&r);

    run_tool(d, "cp", "src.txt s2.txt dst.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cp: target 'dst.txt' is not a directory") != NULL);
    run_result_free(&r);

    // read-only: the destination protects itself, -f overrides; the attribute is copied
    make_sized_file(d, "ro.txt", 3, FILE_ATTRIBUTE_READONLY);
    run_tool(d, "cp", "src.txt ro.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "Permission denied") != NULL);
    run_result_free(&r);
    run_tool(d, "cp", "-f src.txt ro.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "ro.txt", "hello");
    run_result_free(&r);

    make_sized_file(d, "rosrc.txt", 2, FILE_ATTRIBUTE_READONLY);
    run_tool(d, "cp", "rosrc.txt rocopy.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK((attributes_in(d, "rocopy.txt") & FILE_ATTRIBUTE_READONLY) != 0);
    run_result_free(&r);

    // times: a new file is new, -p keeps the time of the source
    set_mtime(d, "src.txt", 2001, 6, 15);
    run_tool(d, "cp", "src.txt fresh.txt", NULL, &r);
    CHECK(year_of(d, "fresh.txt") > 2001);
    run_result_free(&r);
    run_tool(d, "cp", "-p src.txt kept.txt", NULL, &r);
    CHECK_INT(year_of(d, "kept.txt"), 2001);
    run_result_free(&r);

    run_tool(d, "cp", "-a tree archive", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "archive\\sub\\b.txt", "B");
    run_result_free(&r);

    run_tool(d, "cp", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: cp") != NULL);
    run_result_free(&r);
}

static void test_mv(void) {
    RunResult r;
    char d[MAX_PATH * 2];

    printf("mv\n");
    snprintf(d, sizeof d, "%s\\mv", workDir);
    reset_dir(d);
    write_text(d, "a.txt", "A");

    run_tool(d, "mv", "a.txt b.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "a.txt"));
    expect_content(d, "b.txt", "A");
    run_result_free(&r);

    run_tool(d, "mv", "-v b.txt c.txt", NULL, &r);
    CHECK_STR(lf_only(r.out), "renamed 'b.txt' -> 'c.txt'\n");
    run_result_free(&r);

    // into a directory
    make_dir_in(d, "dir");
    run_tool(d, "mv", "c.txt dir", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "dir\\c.txt", "A");
    CHECK(!path_exists_in(d, "c.txt"));
    run_result_free(&r);

    // replacing, -n, -i
    write_text(d, "x.txt", "X");
    write_text(d, "y.txt", "Y");
    run_tool(d, "mv", "x.txt y.txt", NULL, &r);
    expect_content(d, "y.txt", "X");
    CHECK(!path_exists_in(d, "x.txt"));
    run_result_free(&r);

    write_text(d, "p.txt", "P");
    write_text(d, "q.txt", "Q");
    run_tool(d, "mv", "-n p.txt q.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "q.txt", "Q");
    CHECK(path_exists_in(d, "p.txt"));
    run_result_free(&r);

    run_tool(d, "mv", "-i p.txt q.txt", "n\n", &r);
    expect_content(d, "q.txt", "Q");
    CHECK(strstr(r.err, "mv: overwrite 'q.txt'?") != NULL);
    run_result_free(&r);
    run_tool(d, "mv", "-i p.txt q.txt", "y\n", &r);
    expect_content(d, "q.txt", "P");
    run_result_free(&r);

    // a read-only destination is replaced, as unlinking does not depend on it
    make_sized_file(d, "ro.txt", 1, FILE_ATTRIBUTE_READONLY);
    write_text(d, "n.txt", "N");
    run_tool(d, "mv", "n.txt ro.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "ro.txt", "N");
    run_result_free(&r);

    // directories
    make_dir_in(d, "d1");
    write_text(d, "d1\\f.txt", "F");
    run_tool(d, "mv", "d1 d2", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "d1"));
    expect_content(d, "d2\\f.txt", "F");
    run_result_free(&r);

    make_dir_in(d, "d3");
    run_tool(d, "mv", "d2 d3", NULL, &r);
    expect_content(d, "d3\\d2\\f.txt", "F");
    run_result_free(&r);

    run_tool(d, "mv", "d3 d3\\d2", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot move 'd3' to a subdirectory of itself") != NULL);
    run_result_free(&r);

    write_text(d, "file.txt", "f");
    run_tool(d, "mv", "d3 file.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot overwrite non-directory 'file.txt' with directory 'd3'") != NULL);
    run_result_free(&r);

    // several sources, target directory option
    make_dir_in(d, "target");
    write_text(d, "m1.txt", "1");
    write_text(d, "m2.txt", "2");
    run_tool(d, "mv", "m1.txt m2.txt target", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "target\\m1.txt", "1");
    expect_content(d, "target\\m2.txt", "2");
    run_result_free(&r);

    make_dir_in(d, "target2");
    write_text(d, "m3.txt", "3");
    run_tool(d, "mv", "-t target2 m3.txt", NULL, &r);
    expect_content(d, "target2\\m3.txt", "3");
    run_result_free(&r);

    write_text(d, "m4.txt", "4");
    write_text(d, "m5.txt", "5");
    run_tool(d, "mv", "m4.txt m5.txt file.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "mv: target 'file.txt' is not a directory") != NULL);
    run_result_free(&r);

    // errors
    run_tool(d, "mv", "nosuch.txt z.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "mv: cannot stat 'nosuch.txt': No such file or directory") != NULL);
    run_result_free(&r);

    run_tool(d, "mv", "m4.txt m4.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "'m4.txt' and 'm4.txt' are the same file") != NULL);
    run_result_free(&r);

    run_tool(d, "mv", "m4.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "missing destination file operand after 'm4.txt'") != NULL);
    run_result_free(&r);

    // names that differ only in case are one file to Windows, but a rename is fine
    write_text(d, "case.txt", "c");
    run_tool(d, "mv", "case.txt CASE.TXT", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    run_tool(d, "ls", "", NULL, &r);
    CHECK(has_line(r.out, "CASE.TXT"));
    CHECK(!has_line(r.out, "case.txt"));
    run_result_free(&r);

    run_tool(d, "mv", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: mv") != NULL);
    run_result_free(&r);
}

int main(void) {
    make_work_dir("commands", workDir, sizeof workDir);
    expected_virtual(workDir, virtualWorkDir, sizeof virtualWorkDir);

    test_echo();
    test_arch();
    test_pwd();
    test_cat();
    test_ls();
    test_rm();
    test_cp();
    test_mv();
    return test_summary("commands_test");
}
