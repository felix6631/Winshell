#include "vpath.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int is_separator(char c) {
    return c == '/' || c == '\\';
}

static int has_drive(const char* path) {
    return isalpha((unsigned char)path[0]) && path[1] == ':';
}

/** Appends one component to the normalized absolute path in out. */
static int push_component(char* out, size_t size, const char* name, size_t length) {
    size_t used = strlen(out);

    if (length == 0 || (length == 1 && name[0] == '.'))
        return 0;

    if (length == 2 && name[0] == '.' && name[1] == '.') {
        char* last = strrchr(out, '/');
        if (last != out)
            *last = '\0';       // drop the last component
        else
            out[1] = '\0';      // "/.." is "/"
        return 0;
    }

    // "/" + name, or "/a" + "/" + name
    if (used + (used > 1 ? 1 : 0) + length + 1 > size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (used > 1)
        out[used++] = '/';
    memcpy(out + used, name, length);
    out[used + length] = '\0';

    // the drive is spelled in lower case
    if (used == 1 && length == 1)
        out[1] = (char)tolower((unsigned char)out[1]);
    return 0;
}

/** Splits path into components and applies them to out. */
static int apply_path(char* out, size_t size, const char* path) {
    const char* start = path;

    for (;;) {
        const char* end = start;
        while (*end != '\0' && !is_separator(*end))
            end++;
        if (push_component(out, size, start, (size_t)(end - start)) != 0)
            return -1;
        if (*end == '\0')
            return 0;
        start = end + 1;
    }
}

int vpath_resolve(const char* base, const char* path, char* out, size_t size) {
    if (size < 2) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(out, "/");

    if (has_drive(path)) {
        // C:\dir or C:/dir; the drive is the first component of the virtual path
        char drive[2];
        drive[0] = path[0];
        drive[1] = '\0';
        if (push_component(out, size, drive, 1) != 0)
            return -1;
        return apply_path(out, size, path + 2);
    }

    if (path[0] == '\\') {
        errno = ENOENT;     // the root of "the current drive" or a UNC path: not in the tree
        return -1;
    }

    if (path[0] != '/' && apply_path(out, size, base) != 0)
        return -1;
    return apply_path(out, size, path);
}

int vpath_from_native(const char* native, char* out, size_t size) {
    if (!has_drive(native))
        return -1;
    return vpath_resolve("/", native, out, size);
}

int vpath_absolute_to_native(const char* absolute, char* out, size_t size) {
    const char* p = absolute;
    size_t length = strlen(absolute);

    out[0] = '\0';
    if (strcmp(absolute, "/") == 0)
        return VPATH_ROOT;

    if (!(length >= 2 && p[0] == '/' && isalpha((unsigned char)p[1]) && (p[2] == '/' || p[2] == '\0'))) {
        errno = ENOENT;
        return -1;
    }

    // "/c" -> "C:\", "/c/a/b" -> "C:\a\b"
    if (length + 2 > size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    out[0] = (char)toupper((unsigned char)p[1]);
    out[1] = ':';
    {
        // p[2] is the '/' after the drive, or the end of the path for the drive's root;
        // the characters from p[2] on land at the same index in out
        size_t i;
        for (i = 2; p[i] != '\0'; i++)
            out[i] = (p[i] == '/') ? '\\' : p[i];
        if (i == 2)
            out[i++] = '\\';
        out[i] = '\0';
    }
    return 0;
}

int vpath_root_native(char* out, size_t size) {
    char windows[MAX_PATH];
    UINT length = GetWindowsDirectoryA(windows, sizeof windows);

    if (length < 3 || size < 4 || !has_drive(windows))
        return -1;
    out[0] = windows[0];
    out[1] = ':';
    out[2] = '\\';
    out[3] = '\0';
    return 0;
}

/** Compares two Windows directories, ignoring case and a trailing backslash. */
static int same_native_directory(const char* a, const char* b) {
    size_t lengthA = strlen(a), lengthB = strlen(b);

    if (lengthA > 3 && a[lengthA - 1] == '\\')
        lengthA--;
    if (lengthB > 3 && b[lengthB - 1] == '\\')
        lengthB--;
    return lengthA == lengthB && _strnicmp(a, b, lengthA) == 0;
}

int vpath_getcwd(char* out, size_t size) {
    char native[VPATH_MAX];
    char rootNative[8];
    char native_pwd[VPATH_MAX];
    const char* pwd = getenv("PWD");
    DWORD length = GetCurrentDirectoryA(sizeof native, native);

    if (length == 0 || length >= sizeof native)
        return -1;

    // PWD is only believed while it still describes the current directory
    if (pwd != NULL && pwd[0] == '/') {
        char normalized[VPATH_MAX];
        if (vpath_resolve("/", pwd, normalized, sizeof normalized) == 0) {
            int kind = vpath_absolute_to_native(normalized, native_pwd, sizeof native_pwd);
            int valid = 0;

            if (kind == VPATH_ROOT)
                valid = vpath_root_native(rootNative, sizeof rootNative) == 0 &&
                        same_native_directory(rootNative, native);
            else if (kind == 0)
                valid = same_native_directory(native_pwd, native);

            if (valid) {
                if (strlen(normalized) + 1 > size)
                    return -1;
                strcpy(out, normalized);
                return 0;
            }
        }
    }

    return vpath_from_native(native, out, size);
}

int vpath_to_native(const char* path, char* out, size_t size) {
    char base[VPATH_MAX], absolute[VPATH_MAX];

    // not part of the virtual tree: hand it to Windows as it is
    if (path[0] == '\\') {
        if (strlen(path) + 1 > size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(out, path);
        return 0;
    }

    if (has_drive(path) && !(path[2] == '\0' || is_separator(path[2]))) {
        // "C:file" is relative to the current directory of that drive
        if (strlen(path) + 1 > size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(out, path);
        return 0;
    }

    if (vpath_getcwd(base, sizeof base) != 0)
        strcpy(base, "/");

    if (vpath_resolve(base, path, absolute, sizeof absolute) != 0)
        return -1;

    return vpath_absolute_to_native(absolute, out, size);
}
