/*
 * What happens when Windows does not let go of a file: rm, mv, cp and the readers meet a file
 * that another process has open without sharing, a directory that is open or somebody's current
 * directory, and a tree of which one file is locked. Windows reports these as sharing violations;
 * the tools have to say so, leave things as they were and go on with the rest.
 *
 * And the moves between volumes: mv copies and removes then. WINSHELL_TEST_CROSS_DEVICE makes mv take that
 * path on a single volume, so the same code runs here; with a second fixed drive it is tried for real.
 */

#include "testtools.h"

static HANDLE hold(const char* directory, const char* name, DWORD access, DWORD share) {
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\%s", directory, name);
    return CreateFileA(path, access, share, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
}

/** The reason of a refused operation: busy, or denied, depending on the file system and the access. */
static int refusal(const char* text) {
    return strstr(text, "Device or resource busy") != NULL || strstr(text, "Permission denied") != NULL;
}

static void test_locked_file(void) {
    char d[MAX_PATH * 2];
    HANDLE h;
    RunResult r;

    printf("a file that is open without sharing\n");
    snprintf(d, sizeof d, "%s\\locked", workDir);
    reset_dir(d);
    write_text(d, "locked.txt", "locked content");
    write_text(d, "src.txt", "source");

    h = hold(d, "locked.txt", GENERIC_READ | GENERIC_WRITE, 0);
    CHECK(h != INVALID_HANDLE_VALUE);

    run_tool(d, "rm", "locked.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rm: cannot remove 'locked.txt'") != NULL && refusal(r.err));
    CHECK(path_exists_in(d, "locked.txt"));
    run_result_free(&r);

    run_tool(d, "rm", "-f locked.txt", NULL, &r);       // -f is for files that are not there
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    run_tool(d, "mv", "locked.txt moved.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "mv: cannot move 'locked.txt' to 'moved.txt'") != NULL && refusal(r.err));
    CHECK(path_exists_in(d, "locked.txt") && !path_exists_in(d, "moved.txt"));
    run_result_free(&r);

    run_tool(d, "cp", "locked.txt copy.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cp: cannot open 'locked.txt' for reading") != NULL && refusal(r.err));
    CHECK(!path_exists_in(d, "copy.txt"));
    run_result_free(&r);

    // the destination locked: it is not touched
    run_tool(d, "cp", "-f src.txt locked.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cp: cannot create regular file 'locked.txt'") != NULL && refusal(r.err));
    run_result_free(&r);

    // the readers say so, and exit with an error
    run_tool(d, "cat", "locked.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cat: locked.txt:") != NULL && refusal(r.err));
    run_result_free(&r);
    run_tool(d, "head", "locked.txt src.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.out, "source") != NULL);             // the other file is still shown
    run_result_free(&r);
    run_tool(d, "wc", "-c locked.txt src.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.out, "src.txt") != NULL);
    run_result_free(&r);
    run_tool(d, "sort", "locked.txt", NULL, &r);
    CHECK_INT(r.exitCode, 2);
    run_result_free(&r);

    // nothing of it was changed
    CloseHandle(h);
    expect_content(d, "locked.txt", "locked content");

    // once it is let go, everything works
    run_tool(d, "mv", "locked.txt moved.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    run_tool(d, "rm", "moved.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "moved.txt"));
    run_result_free(&r);
}

static void test_shared_for_reading(void) {
    char d[MAX_PATH * 2];
    HANDLE h;
    RunResult r;

    printf("a file that is open for reading, and shared for reading only\n");
    snprintf(d, sizeof d, "%s\\readshared", workDir);
    reset_dir(d);
    write_text(d, "f.txt", "readable");

    h = hold(d, "f.txt", GENERIC_READ, FILE_SHARE_READ);
    CHECK(h != INVALID_HANDLE_VALUE);

    // reading and copying are fine
    run_tool(d, "cat", "f.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK_STR(r.out, "readable");
    run_result_free(&r);
    run_tool(d, "cp", "f.txt g.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(d, "g.txt", "readable");
    run_result_free(&r);

    // removing and renaming need the right to delete, which the other one did not give
    run_tool(d, "rm", "f.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(refusal(r.err));
    CHECK(path_exists_in(d, "f.txt"));
    run_result_free(&r);
    run_tool(d, "mv", "f.txt h.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(path_exists_in(d, "f.txt") && !path_exists_in(d, "h.txt"));
    run_result_free(&r);

    CloseHandle(h);
    run_tool(d, "rm", "f.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
}

static void test_locked_directory(void) {
    char d[MAX_PATH * 2];
    HANDLE h;
    RunResult r;

    printf("a directory that is open\n");
    snprintf(d, sizeof d, "%s\\lockeddir", workDir);
    reset_dir(d);
    make_dir_in(d, "sub");
    write_text(d, "sub\\inside.txt", "in");
    make_dir_in(d, "empty");

    // opened for reading, without the share for deleting
    h = hold(d, "empty", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE);
    CHECK(h != INVALID_HANDLE_VALUE);
    run_tool(d, "rmdir", "empty", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "rmdir: failed to remove 'empty'") != NULL);
    CHECK(path_exists_in(d, "empty"));
    run_result_free(&r);
    run_tool(d, "mv", "empty renamed", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(path_exists_in(d, "empty") && !path_exists_in(d, "renamed"));
    run_result_free(&r);
    run_tool(d, "rm", "-d empty", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(path_exists_in(d, "empty"));
    run_result_free(&r);
    CloseHandle(h);
    run_tool(d, "rmdir", "empty", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    // the current directory of a program that is still running cannot be removed
    {
        Process p;
        char program[MAX_PATH * 2], busy[MAX_PATH * 2];
        RunResult finished;

        make_dir_in(d, "busy");
        snprintf(busy, sizeof busy, "%s\\busy", d);
        bin_path("cat", program, sizeof program);
        CHECK(proc_start(&p, program, "", busy) == 0);

        run_tool(d, "rmdir", "busy", NULL, &r);
        CHECK_INT(r.exitCode, 1);
        CHECK(strstr(r.err, "rmdir: failed to remove 'busy'") != NULL);
        CHECK(path_exists_in(d, "busy"));
        run_result_free(&r);

        run_tool(d, "rm", "-r busy", NULL, &r);
        CHECK_INT(r.exitCode, 1);
        CHECK(path_exists_in(d, "busy"));
        run_result_free(&r);

        // when the program ends the directory is free
        proc_close_input(&p);
        CHECK(proc_wait(&p, 5000, &finished) == 0);
        run_result_free(&finished);
        run_tool(d, "rmdir", "busy", NULL, &r);
        CHECK_INT(r.exitCode, 0);
        CHECK(!path_exists_in(d, "busy"));
        run_result_free(&r);
    }
}

/** A tree of which one file is locked: everything else is removed or copied, the exit code tells. */
static void test_locked_in_tree(void) {
    char d[MAX_PATH * 2];
    HANDLE h;
    RunResult r;

    printf("one locked file in a tree\n");
    snprintf(d, sizeof d, "%s\\tree", workDir);
    reset_dir(d);
    make_dir_in(d, "t");
    make_dir_in(d, "t\\sub");
    write_text(d, "t\\a.txt", "A");
    write_text(d, "t\\sub\\locked.txt", "L");
    write_text(d, "t\\sub\\b.txt", "B");
    write_text(d, "t\\z.txt", "Z");

    h = hold(d, "t\\sub\\locked.txt", GENERIC_READ | GENERIC_WRITE, 0);
    CHECK(h != INVALID_HANDLE_VALUE);

    // copy: the files that can be are copied
    run_tool(d, "cp", "-r t u", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot open 't/sub/locked.txt' for reading") != NULL);
    expect_content(d, "u\\a.txt", "A");
    expect_content(d, "u\\sub\\b.txt", "B");
    expect_content(d, "u\\z.txt", "Z");
    CHECK(!path_exists_in(d, "u\\sub\\locked.txt"));
    run_result_free(&r);

    // remove: the other files go, the directories with the locked file stay
    run_tool(d, "rm", "-r t", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "t/sub/locked.txt") != NULL);
    CHECK(!path_exists_in(d, "t\\a.txt") && !path_exists_in(d, "t\\z.txt") && !path_exists_in(d, "t\\sub\\b.txt"));
    CHECK(path_exists_in(d, "t\\sub\\locked.txt"));
    run_result_free(&r);

    // a second time, still the same, and not a crash
    run_tool(d, "rm", "-rf t", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    CloseHandle(h);
    run_tool(d, "rm", "-r t", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(d, "t"));
    run_result_free(&r);
}

/** Runs one check of a move of files and trees; the environment decides which way it is done. */
static void moves_between_volumes(const char* source, const char* destination, const char* label) {
    char s[MAX_PATH * 2], t[MAX_PATH * 2], arguments[MAX_PATH * 5];
    RunResult r;

    printf("moves, %s\n", label);
    snprintf(s, sizeof s, "%s", source);
    snprintf(t, sizeof t, "%s", destination);
    reset_dir(s);
    reset_dir(t);

    // a file, with its time
    write_text(s, "file.txt", "file content");
    set_mtime(s, "file.txt", 2001, 6, 15);
    snprintf(arguments, sizeof arguments, "-v file.txt \"%s\"", t);
    run_tool(s, "mv", arguments, NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strstr(r.out, "renamed 'file.txt'") != NULL);
    CHECK(!path_exists_in(s, "file.txt"));
    expect_content(t, "file.txt", "file content");
    CHECK_INT(year_of(t, "file.txt"), 2001);
    run_result_free(&r);

    // a tree, with a read-only file in it, and a file of more than one buffer
    make_dir_in(s, "tree");
    make_dir_in(s, "tree\\sub");
    make_dir_in(s, "tree\\empty");
    write_text(s, "tree\\a.txt", "A");
    write_text(s, "tree\\sub\\b.txt", "B");
    make_sized_file(s, "tree\\sub\\ro.txt", 3, FILE_ATTRIBUTE_READONLY);
    make_sized_file(s, "tree\\big.bin", 300000, FILE_ATTRIBUTE_NORMAL);
    snprintf(arguments, sizeof arguments, "tree \"%s\"", t);
    run_tool(s, "mv", arguments, NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(s, "tree"));
    expect_content(t, "tree\\a.txt", "A");
    expect_content(t, "tree\\sub\\b.txt", "B");
    CHECK((attributes_in(t, "tree\\sub\\ro.txt") & FILE_ATTRIBUTE_READONLY) != 0);
    CHECK(path_exists_in(t, "tree\\empty"));
    {
        size_t length = 0;
        char* big = read_file(t, "tree\\big.bin", &length);
        CHECK_INT(length, 300000);
        free(big);
    }
    run_result_free(&r);

    // onto an existing file
    write_text(s, "over.txt", "new");
    write_text(t, "over.txt", "old");
    snprintf(arguments, sizeof arguments, "over.txt \"%s\"", t);
    run_tool(s, "mv", arguments, NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(t, "over.txt", "new");
    CHECK(!path_exists_in(s, "over.txt"));
    run_result_free(&r);

    // -n leaves both as they are
    write_text(s, "keep.txt", "source");
    write_text(t, "keep.txt", "dest");
    snprintf(arguments, sizeof arguments, "-n keep.txt \"%s\"", t);
    run_tool(s, "mv", arguments, NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(t, "keep.txt", "dest");
    expect_content(s, "keep.txt", "source");
    run_result_free(&r);

    // a locked destination: the move fails, the source is still there
    {
        HANDLE h;
        write_text(s, "locked-target.txt", "payload");
        write_text(t, "locked-target.txt", "held");
        h = hold(t, "locked-target.txt", GENERIC_READ | GENERIC_WRITE, 0);
        snprintf(arguments, sizeof arguments, "locked-target.txt \"%s\"", t);
        run_tool(s, "mv", arguments, NULL, &r);
        CHECK_INT(r.exitCode, 1);
        expect_content(s, "locked-target.txt", "payload");
        run_result_free(&r);
        CloseHandle(h);
        expect_content(t, "locked-target.txt", "held");
    }

    // a locked source: nothing is lost
    {
        HANDLE h;
        write_text(s, "locked-source.txt", "precious");
        h = hold(s, "locked-source.txt", GENERIC_READ | GENERIC_WRITE, 0);
        snprintf(arguments, sizeof arguments, "locked-source.txt \"%s\"", t);
        run_tool(s, "mv", arguments, NULL, &r);
        CHECK_INT(r.exitCode, 1);
        CHECK(!path_exists_in(t, "locked-source.txt"));
        run_result_free(&r);
        CloseHandle(h);
        expect_content(s, "locked-source.txt", "precious");
    }

    // a missing source is no business of the copy
    snprintf(arguments, sizeof arguments, "nosuch.txt \"%s\"", t);
    run_tool(s, "mv", arguments, NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cannot stat 'nosuch.txt'") != NULL);
    run_result_free(&r);
}

static void test_cross_device_path(void) {
    char source[MAX_PATH * 2], destination[MAX_PATH * 2];

    snprintf(source, sizeof source, "%s\\cross-source", workDir);
    snprintf(destination, sizeof destination, "%s\\cross-destination", workDir);

    // the move of mv with the rename refused: the copy-and-remove path, on one volume
    _putenv_s("WINSHELL_TEST_CROSS_DEVICE", "1");
    moves_between_volumes(source, destination, "copy and remove (WINSHELL_TEST_CROSS_DEVICE)");
    _putenv_s("WINSHELL_TEST_CROSS_DEVICE", "");

    // and the same checks with the plain rename, so that both ways give the same results
    moves_between_volumes(source, destination, "rename");
}

/** A second fixed drive with room to write: the move between volumes for real. */
static void test_second_drive(void) {
    DWORD mask = GetLogicalDrives();
    char own = (char)toupper((unsigned char)workDir[0]);
    char drive;

    for (drive = 'A'; drive <= 'Z'; drive++) {
        char root[4], directory[MAX_PATH], probe[MAX_PATH];
        HANDLE h;

        if (drive == own || !(mask & (1u << (drive - 'A'))))
            continue;
        snprintf(root, sizeof root, "%c:\\", drive);
        if (GetDriveTypeA(root) != DRIVE_FIXED)
            continue;

        // a directory of our own on it, if it can be written to
        snprintf(directory, sizeof directory, "%c:\\winshell-test-%lu", drive, GetCurrentProcessId());
        if (!CreateDirectoryA(directory, NULL))
            continue;
        snprintf(probe, sizeof probe, "%s\\probe", directory);
        h = CreateFileA(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            RemoveDirectoryA(directory);
            continue;
        }
        CloseHandle(h);

        {
            char source[MAX_PATH * 2];
            char label[64];
            snprintf(source, sizeof source, "%s\\second-drive-source", workDir);
            snprintf(label, sizeof label, "from %c: to %c:, for real", own, drive);
            {
                char destination[MAX_PATH * 2];
                snprintf(destination, sizeof destination, "%s\\moves", directory);
                CreateDirectoryA(destination, NULL);
                moves_between_volumes(source, destination, label);
                reset_dir(destination);
                RemoveDirectoryA(destination);
            }
        }
        RemoveDirectoryA(directory);
        return;
    }
    printf("moves between two drives: skipped, there is no second fixed drive to write to\n");
}

int main(void) {
    tools_init("sharing_test");
    test_locked_file();
    test_shared_for_reading();
    test_locked_directory();
    test_locked_in_tree();
    test_cross_device_path();
    test_second_drive();
    return test_summary("sharing_test");
}
