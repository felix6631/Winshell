#ifndef VPATH_H
#define VPATH_H

#include <stddef.h>

/*
 * The virtual file system.
 *
 * Like on UNIX there is one root directory, "/", and every drive is placed
 * below it as a single lower case letter: C:\Users\me is /c/Users/me.
 * The root itself has no Windows counterpart; it only contains the drives.
 *
 * Programs get paths exactly as the user typed them and convert them here
 * when they hand them to Windows. Both notations are accepted everywhere,
 * so "C:\Users\me" keeps working. "\\server\share" and a path starting with
 * a single backslash are not part of the virtual tree and are passed to
 * Windows unchanged.
 *
 * The current directory is the one of the process, as Windows has it. The
 * environment variable PWD, set by the shell, adds to that the one place the
 * virtual tree has and Windows has not: the root directory "/".
 *
 * All functions fill a caller supplied buffer; VPATH_MAX is always enough
 * for Windows paths.
 */

#define VPATH_MAX 1024

/** True for a path separator: Windows takes the slash and the backslash alike. */
static __inline int vpath_is_separator(char c) {
    return c == '/' || c == '\\';
}

/** Result of vpath_to_native: the path is the virtual root "/", which has no Windows path. */
#define VPATH_ROOT 1

/**
 * Converts the Windows path "C:\Users\me" to "/c/Users/me".
 * Returns 0, or -1 when it is not on a drive (a UNC path).
 */
int vpath_from_native(const char* native, char* out, size_t size);

/**
 * Resolves path against the absolute virtual directory base, giving a
 * normalized absolute virtual path: "." and ".." are removed and
 * "/.." is "/". path is relative, virtual absolute, or a Windows path with
 * a drive letter.
 * Returns 0, or -1 when out is too small or path is not on a drive.
 */
int vpath_resolve(const char* base, const char* path, char* out, size_t size);

/**
 * Converts an absolute, normalized virtual path to a Windows path.
 * Returns 0, VPATH_ROOT for "/" (out is left empty), or -1 with errno
 * ENOENT when the first component is not a drive letter.
 */
int vpath_absolute_to_native(const char* absolute, char* out, size_t size);

/**
 * Gets the current directory as an absolute virtual path.
 * Returns 0, or -1 when the current directory is not on a drive.
 */
int vpath_getcwd(char* out, size_t size);

/**
 * Converts a path from the user to the Windows path to open.
 * Returns 0, VPATH_ROOT for the virtual root, or -1 with errno set to
 * ENOENT (no such drive, or a relative path while in the root directory)
 * or ENAMETOOLONG.
 */
int vpath_to_native(const char* path, char* out, size_t size);

/**
 * Lists the drives that exist, which are the entries of the virtual root.
 * letters receives lower case letters in alphabetical order, with a
 * terminating NUL, so it has to hold 27 characters. Returns their number.
 */
size_t vpath_drives(char letters[27]);

/** The directory of the Windows installation's drive root, which the current directory is while in "/". */
int vpath_root_native(char* out, size_t size);

#endif //VPATH_H
