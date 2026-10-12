#include "fsutil.h"
#include "vpath.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_BUFFER 1024

/* ---- small helpers ------------------------------------------------------------ */

const char* fs_strerror(DWORD error) {
    static char other[256];

    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_DRIVE:
        case ERROR_BAD_PATHNAME:
        case ERROR_INVALID_NAME:        // a name with characters Windows does not allow, such as *
            return "No such file or directory";
        case ERROR_ACCESS_DENIED:
        case ERROR_WRITE_PROTECT:
            return "Permission denied";
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return "Device or resource busy";
        case ERROR_DIR_NOT_EMPTY:
            return "Directory not empty";
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            return "File exists";
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL:
            return "No space left on device";
        case ERROR_NOT_SAME_DEVICE:
            return "Invalid cross-device link";
        case ERROR_DIRECTORY:
            return "Not a directory";
        case ERROR_FILENAME_EXCED_RANGE:
            return "File name too long";
        default: {
            DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error,
                                          0, other, sizeof other, NULL);
            while (length > 0 && (other[length - 1] == '\r' || other[length - 1] == '\n' ||
                                  other[length - 1] == ' ' || other[length - 1] == '.'))
                other[--length] = '\0';
            if (length == 0)
                snprintf(other, sizeof other, "Windows error %lu", error);
            return other;
        }
    }
}

int fs_attributes(const char* native, DWORD* attributes) {
    DWORD value = GetFileAttributesA(native);
    if (value == INVALID_FILE_ATTRIBUTES)
        return -1;
    *attributes = value;
    return 0;
}

static int is_separator(char c) {
    return c == '/' || c == '\\';
}

void fs_basename(const char* path, char* out, size_t size) {
    size_t length = strlen(path), end = length, start;

    while (end > 1 && is_separator(path[end - 1]))
        end--;
    start = end;
    while (start > 0 && !is_separator(path[start - 1]))
        start--;
    // "C:" is a drive, not a name
    if (end - start == 2 && path[start + 1] == ':')
        start = end;
    if (end - start + 1 > size) {
        out[0] = '\0';
        return;
    }
    memcpy(out, path + start, end - start);
    out[end - start] = '\0';
}

void fs_join(const char* directory, const char* name, char* out, size_t size) {
    size_t length = strlen(directory);
    if (length == 0)
        snprintf(out, size, "%s", name);
    else if (is_separator(directory[length - 1]))
        snprintf(out, size, "%s%s", directory, name);
    else
        snprintf(out, size, "%s/%s", directory, name);
}

/** Like fs_join, but with the Windows separator, for paths handed to Windows. */
static void join_native(const char* directory, const char* name, char* out, size_t size) {
    size_t length = strlen(directory);
    if (length == 0 || is_separator(directory[length - 1]))
        snprintf(out, size, "%s%s", directory, name);
    else
        snprintf(out, size, "%s\\%s", directory, name);
}

int fs_stdin_is_console(void) {
    DWORD mode;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
}

int fs_confirm(const char* format, ...) {
    char answer[64];
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fflush(stderr);

    if (fgets(answer, sizeof answer, stdin) == NULL)
        return 0;
    return answer[0] == 'y' || answer[0] == 'Y';
}

/** Reads the identity of a file: volume and index. Returns 0 on success. */
static int file_identity(const char* native, DWORD* volume, DWORD* high, DWORD* low) {
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE h = CreateFileA(native, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    int result = -1;

    if (h == INVALID_HANDLE_VALUE)
        return -1;
    if (GetFileInformationByHandle(h, &info)) {
        *volume = info.dwVolumeSerialNumber;
        *high = info.nFileIndexHigh;
        *low = info.nFileIndexLow;
        result = 0;
    }
    CloseHandle(h);
    return result;
}

int fs_same_file(const char* nativeA, const char* nativeB) {
    DWORD va, ha, la, vb, hb, lb;
    if (file_identity(nativeA, &va, &ha, &la) != 0 || file_identity(nativeB, &vb, &hb, &lb) != 0)
        return 0;
    return va == vb && ha == hb && la == lb;
}

int fs_is_inside(const char* nativeParent, const char* nativeChild) {
    char parent[PATH_BUFFER], child[PATH_BUFFER];
    size_t length;

    if (GetFullPathNameA(nativeParent, sizeof parent, parent, NULL) == 0 ||
        GetFullPathNameA(nativeChild, sizeof child, child, NULL) == 0)
        return 0;

    length = strlen(parent);
    while (length > 3 && is_separator(parent[length - 1]))
        parent[--length] = '\0';

    if (_strnicmp(parent, child, length) != 0)
        return 0;
    // the match has to end at a component boundary
    return child[length] == '\0' || is_separator(child[length]) || is_separator(parent[length - 1]);
}

static void report(const FsOptions* o, const char* what, const char* path, const char* reason) {
    fprintf(stderr, "%s: %s '%s': %s\n", o->program, what, path, reason);
}

static void report_last(const FsOptions* o, const char* what, const char* path) {
    report(o, what, path, fs_strerror(GetLastError()));
}

static int is_directory_attributes(DWORD attributes) {
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int is_link_attributes(DWORD attributes) {
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

/* ---- copying ------------------------------------------------------------------ */

static int copy_file(const FsOptions* o, const char* srcDisplay, const char* srcNative,
                     const char* dstDisplay, const char* dstNative, DWORD srcAttributes) {
    DWORD dstAttributes;
    HANDLE in, out;
    static char buffer[64 * 1024];
    DWORD got, put;
    int failed = 0;

    if (fs_attributes(dstNative, &dstAttributes) == 0) {
        if (is_directory_attributes(dstAttributes)) {
            fprintf(stderr, "%s: cannot overwrite directory '%s' with non-directory\n", o->program, dstDisplay);
            return 1;
        }
        if (fs_same_file(srcNative, dstNative)) {
            fprintf(stderr, "%s: '%s' and '%s' are the same file\n", o->program, srcDisplay, dstDisplay);
            return 1;
        }
        if (o->noClobber)
            return 0;
        if (o->interactive && !fs_confirm("%s: overwrite '%s'? ", o->program, dstDisplay))
            return 0;
        if (dstAttributes & FILE_ATTRIBUTE_READONLY) {
            if (!o->force) {
                report(o, "cannot create regular file", dstDisplay, "Permission denied");
                return 1;
            }
            SetFileAttributesA(dstNative, FILE_ATTRIBUTE_NORMAL);
        }
    }

    in = CreateFileA(srcNative, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL, NULL);
    if (in == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "%s: cannot open '%s' for reading: %s\n", o->program, srcDisplay, fs_strerror(GetLastError()));
        return 1;
    }

    out = CreateFileA(dstNative, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) {
        report_last(o, "cannot create regular file", dstDisplay);
        CloseHandle(in);
        return 1;
    }

    while (ReadFile(in, buffer, sizeof buffer, &got, NULL) && got > 0) {
        if (!WriteFile(out, buffer, got, &put, NULL) || put != got) {
            report_last(o, "error writing", dstDisplay);
            failed = 1;
            break;
        }
    }

    if (o->preserve) {
        FILETIME created, accessed, written;
        if (GetFileTime(in, &created, &accessed, &written))
            SetFileTime(out, &created, &accessed, &written);
    }
    CloseHandle(in);
    CloseHandle(out);
    if (failed)
        return 1;

    // like the mode bits on UNIX: a read-only file stays read-only; -p keeps the other attributes too
    {
        DWORD keep = srcAttributes & (o->preserve
                                          ? (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM |
                                             FILE_ATTRIBUTE_ARCHIVE)
                                          : FILE_ATTRIBUTE_READONLY);
        if (keep & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
            SetFileAttributesA(dstNative, keep);
    }

    if (o->verbose)
        printf("'%s' -> '%s'\n", srcDisplay, dstDisplay);
    return 0;
}

static int copy_tree(const FsOptions* o, const char* srcDisplay, const char* srcNative,
                     const char* dstDisplay, const char* dstNative) {
    DWORD dstAttributes;
    WIN32_FIND_DATAA data;
    HANDLE find;
    char pattern[PATH_BUFFER];
    int status = 0;

    if (fs_attributes(dstNative, &dstAttributes) == 0) {
        if (!is_directory_attributes(dstAttributes)) {
            fprintf(stderr, "%s: cannot overwrite non-directory '%s' with directory '%s'\n",
                    o->program, dstDisplay, srcDisplay);
            return 1;
        }
    } else {
        if (!CreateDirectoryA(dstNative, NULL)) {
            report_last(o, "cannot create directory", dstDisplay);
            return 1;
        }
        if (o->verbose)
            printf("'%s' -> '%s'\n", srcDisplay, dstDisplay);
    }

    join_native(srcNative, "*", pattern, sizeof pattern);
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        report_last(o, "cannot open directory", srcDisplay);
        return 1;
    }

    do {
        char childSrcDisplay[PATH_BUFFER], childSrcNative[PATH_BUFFER];
        char childDstDisplay[PATH_BUFFER], childDstNative[PATH_BUFFER];

        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
            continue;

        fs_join(srcDisplay, data.cFileName, childSrcDisplay, sizeof childSrcDisplay);
        join_native(srcNative, data.cFileName, childSrcNative, sizeof childSrcNative);
        fs_join(dstDisplay, data.cFileName, childDstDisplay, sizeof childDstDisplay);
        join_native(dstNative, data.cFileName, childDstNative, sizeof childDstNative);

        if (is_directory_attributes(data.dwFileAttributes) && is_link_attributes(data.dwFileAttributes)) {
            fprintf(stderr, "%s: omitting symbolic link '%s'\n", o->program, childSrcDisplay);
            continue;
        }
        if (fs_copy(o, childSrcDisplay, childSrcNative, childDstDisplay, childDstNative) != 0)
            status = 1;
    } while (FindNextFileA(find, &data));
    FindClose(find);

    if (o->preserve) {
        HANDLE in = CreateFileA(srcNative, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        HANDLE out = CreateFileA(dstNative, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (in != INVALID_HANDLE_VALUE && out != INVALID_HANDLE_VALUE) {
            FILETIME created, accessed, written;
            if (GetFileTime(in, &created, &accessed, &written))
                SetFileTime(out, &created, &accessed, &written);
        }
        if (in != INVALID_HANDLE_VALUE)
            CloseHandle(in);
        if (out != INVALID_HANDLE_VALUE)
            CloseHandle(out);
    }
    return status;
}

int fs_copy(const FsOptions* o, const char* srcDisplay, const char* srcNative,
            const char* dstDisplay, const char* dstNative) {
    DWORD attributes;

    if (fs_attributes(srcNative, &attributes) != 0) {
        report_last(o, "cannot stat", srcDisplay);
        return 1;
    }

    if (is_directory_attributes(attributes)) {
        if (!o->recursive) {
            fprintf(stderr, "%s: -r not specified; omitting directory '%s'\n", o->program, srcDisplay);
            return 1;
        }
        if (fs_is_inside(srcNative, dstNative)) {
            fprintf(stderr, "%s: cannot copy a directory, '%s', into itself, '%s'\n",
                    o->program, srcDisplay, dstDisplay);
            return 1;
        }
        return copy_tree(o, srcDisplay, srcNative, dstDisplay, dstNative);
    }

    return copy_file(o, srcDisplay, srcNative, dstDisplay, dstNative, attributes);
}

/* ---- removing ------------------------------------------------------------------ */

static int remove_directory_contents(const FsOptions* o, const char* display, const char* native) {
    WIN32_FIND_DATAA data;
    HANDLE find;
    char pattern[PATH_BUFFER];
    int status = 0;

    join_native(native, "*", pattern, sizeof pattern);
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        report_last(o, "cannot remove", display);
        return 1;
    }

    do {
        char childDisplay[PATH_BUFFER], childNative[PATH_BUFFER];

        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
            continue;
        fs_join(display, data.cFileName, childDisplay, sizeof childDisplay);
        join_native(native, data.cFileName, childNative, sizeof childNative);
        if (fs_remove(o, childDisplay, childNative) != 0)
            status = 1;
    } while (FindNextFileA(find, &data));
    FindClose(find);
    return status;
}

int fs_remove(const FsOptions* o, const char* display, const char* native) {
    DWORD attributes;
    int isDirectory, isLink;

    if (fs_attributes(native, &attributes) != 0) {
        DWORD error = GetLastError();
        if (o->force && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND))
            return 0;
        report(o, "cannot remove", display, fs_strerror(error));
        return 1;
    }

    isDirectory = is_directory_attributes(attributes);
    isLink = is_link_attributes(attributes);

    if (isDirectory && !isLink) {
        int status = 0;

        if (!o->recursive) {
            if (!o->emptyDirs) {
                report(o, "cannot remove", display, "Is a directory");
                return 1;
            }
        } else {
            if (o->interactive && !fs_confirm("%s: descend into directory '%s'? ", o->program, display))
                return 0;
            status = remove_directory_contents(o, display, native);
            if (status != 0)
                return 1;
        }

        if (o->interactive && !fs_confirm("%s: remove directory '%s'? ", o->program, display))
            return 0;
        if (attributes & FILE_ATTRIBUTE_READONLY)
            SetFileAttributesA(native, FILE_ATTRIBUTE_NORMAL);
        if (!RemoveDirectoryA(native)) {
            report_last(o, "cannot remove", display);
            return 1;
        }
        if (o->verbose)
            printf("removed directory '%s'\n", display);
        return 0;
    }

    // a file, or a link: the link itself goes, not what it points to
    if (o->interactive) {
        if (!fs_confirm("%s: remove %s '%s'? ", o->program, isLink ? "symbolic link" : "regular file", display))
            return 0;
    } else if ((attributes & FILE_ATTRIBUTE_READONLY) && !o->force && fs_stdin_is_console()) {
        if (!fs_confirm("%s: remove write-protected regular file '%s'? ", o->program, display))
            return 0;
    }

    if (attributes & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesA(native, FILE_ATTRIBUTE_NORMAL);

    if (isDirectory ? !RemoveDirectoryA(native) : !DeleteFileA(native)) {
        report_last(o, "cannot remove", display);
        return 1;
    }
    if (o->verbose)
        printf("removed '%s'\n", display);
    return 0;
}

int fs_split_operands(const char* program, const char** operands, int count, const char* targetDirectory,
                      const char** target, int* sourceCount) {
    if (targetDirectory != NULL) {
        *target = targetDirectory;
        *sourceCount = count;
        if (count == 0) {
            fprintf(stderr, "%s: missing file operand\n", program);
            fprintf(stderr, "Try '%s --help' for more information.\n", program);
            return -1;
        }
        return 0;
    }

    if (count == 0) {
        fprintf(stderr, "%s: missing file operand\n", program);
    } else if (count == 1) {
        fprintf(stderr, "%s: missing destination file operand after '%s'\n", program, operands[0]);
    } else {
        *target = operands[count - 1];
        *sourceCount = count - 1;
        return 0;
    }
    fprintf(stderr, "Try '%s --help' for more information.\n", program);
    return -1;
}

int fs_target_is_directory(const char* program, const char* target, const char* targetNative, int mustBeDirectory) {
    DWORD attributes = 0;
    int isDirectory = fs_attributes(targetNative, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if (mustBeDirectory && !isDirectory) {
        fprintf(stderr, "%s: target '%s' is not a directory\n", program, target);
        return -1;
    }
    return isDirectory;
}

int fs_destination(const char* source, const char* target, const char* targetNative, int targetIsDirectory,
                   int dotIsTarget, char* display, size_t displaySize, char* native, size_t nativeSize) {
    char name[VPATH_MAX];

    if (!targetIsDirectory) {
        snprintf(display, displaySize, "%s", target);
        snprintf(native, nativeSize, "%s", targetNative);
        return 0;
    }

    fs_basename(source, name, sizeof name);
    if (dotIsTarget && strcmp(name, ".") == 0) {
        // "cp -r dir/. target" copies the contents: the target itself is the destination
        snprintf(display, displaySize, "%s", target);
        snprintf(native, nativeSize, "%s", targetNative);
        return 0;
    }
    if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;

    fs_join(target, name, display, displaySize);
    join_native(targetNative, name, native, nativeSize);
    return 0;
}
