#ifndef FSUTIL_H
#define FSUTIL_H

#include <stddef.h>
#include <windows.h>

/*
 * File system operations shared by rm, cp and mv.
 *
 * Every function takes two forms of a path: the display name, which is what
 * the user typed and what messages show, and the Windows path to operate on
 * (see vpath_to_native). Messages go to standard error, in the form of the
 * UNIX tools: "program: cannot remove 'name': Permission denied".
 *
 * Windows adaptations:
 *  - Unlinking does not depend on the permissions of the file on UNIX, so a
 *    read-only file is removed like any other one; only a console user is
 *    asked about it, as rm does for write-protected files.
 *  - Windows error codes are reported with the text of the matching errno.
 *  - A directory that is a junction or symbolic link is never entered by a
 *    recursive operation, which also keeps cp -r from running in circles.
 */

typedef struct {
    const char* program;    // name used in messages: "rm", "cp", "mv"
    int force;              // -f
    int interactive;        // -i
    int noClobber;          // -n
    int verbose;            // -v
    int preserve;           // -p: keep times and attributes
    int recursive;          // -r
    int emptyDirs;          // -d (rm): remove empty directories
} FsOptions;

/** Text of an errno-like reason for a Windows error code. */
const char* fs_strerror(DWORD error);

/** Gets the attributes of a path. Returns 0, or -1 with GetLastError set. */
int fs_attributes(const char* native, DWORD* attributes);

/** Non-zero when both paths name the same file or directory (volume and index agree). */
int fs_same_file(const char* nativeA, const char* nativeB);

/** Non-zero when child is parent itself or something below it. Case is ignored. */
int fs_is_inside(const char* nativeParent, const char* nativeChild);

/** Last component of a path, ignoring trailing separators. "dir/" gives "dir". */
void fs_basename(const char* path, char* out, size_t size);

/** "directory" + "/" + "name", with no doubled separator. */
void fs_join(const char* directory, const char* name, char* out, size_t size);

/** Non-zero when standard input is a console, so a person answers the questions. */
int fs_stdin_is_console(void);

/** Prints the question on standard error and reads a line; non-zero for yes. */
int fs_confirm(const char* format, ...);

/**
 * Copies a file or, with options->recursive, a directory tree.
 * Returns 0, or 1 when something could not be copied (reported).
 */
int fs_copy(const FsOptions* options, const char* srcDisplay, const char* srcNative,
            const char* dstDisplay, const char* dstNative);

/**
 * Removes a file or, with options->recursive, a directory tree.
 * Returns 0, or 1 when something could not be removed (reported).
 */
int fs_remove(const FsOptions* options, const char* display, const char* native);

#endif //FSUTIL_H
