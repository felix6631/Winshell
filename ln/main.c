/*
 * ln: makes links between files, as GNU ln does. A hard link is made with CreateHardLink. A symbolic link is a
 * Windows symbolic link (made without the privilege when the system allows it), and a symbolic link to a
 * directory falls back to a junction, which needs no privilege and is what readlink and realpath read as a link.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winioctl.h>
#include "args.h"
#include "fsutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "ln"

static void print_usage(void) {
    puts("Usage: ln [OPTION]... [-T] TARGET LINK_NAME\n"
         "  or:  ln [OPTION]... TARGET\n"
         "  or:  ln [OPTION]... TARGET... DIRECTORY\n"
         "  or:  ln [OPTION]... -t DIRECTORY TARGET...\n"
         "In the 1st form, create a link to TARGET with the name LINK_NAME.\n"
         "In the 2nd form, create a link to TARGET in the current directory.\n"
         "In the 3rd and 4th forms, create links to each TARGET in DIRECTORY.\n"
         "Create hard links by default, symbolic links with --symbolic.\n"
         "\n"
         "  -b                          like --backup but does not accept an argument\n"
         "      --backup[=CONTROL]      make a backup of each existing destination file\n"
         "  -d, -F, --directory         accepted and ignored\n"
         "  -f, --force                 remove existing destination files\n"
         "  -i, --interactive           prompt whether to remove destinations\n"
         "  -L, --logical               accepted (a hard link is made to what a symbolic link points to)\n"
         "  -n, --no-dereference        treat LINK_NAME as a normal file if it is a symbolic link to a directory\n"
         "  -P, --physical              accepted (hard links are made to the symbolic link itself)\n"
         "  -r, --relative              with -s, create links relative to link location\n"
         "  -s, --symbolic              make symbolic links instead of hard links\n"
         "  -S, --suffix=SUFFIX         override the usual backup suffix\n"
         "  -t, --target-directory=DIRECTORY  specify the DIRECTORY in which to create the links\n"
         "  -T, --no-target-directory   treat LINK_NAME as a normal file always\n"
         "  -v, --verbose               print name of each linked file\n"
         "      --help                  display this help and exit\n"
         "      --version               output version information and exit\n"
         "\n"
         "A symbolic link to a directory is made as a junction when Windows does not allow symbolic links.");
}

enum { OPT_BACKUP_SHORT = 1, OPT_BACKUP, OPT_IGNORED, OPT_FORCE, OPT_INTERACTIVE, OPT_NODEREF, OPT_RELATIVE, OPT_SYMBOLIC,
       OPT_SUFFIX, OPT_TARGET_DIR, OPT_NO_TARGET_DIR, OPT_VERBOSE };

static const ArgsOption options[] = {
    { OPT_BACKUP_SHORT, 'b', NULL,              ARGS_NONE },
    { OPT_BACKUP,       0,   "backup",          ARGS_OPTIONAL },
    { OPT_IGNORED,      'd', "directory",       ARGS_NONE },
    { OPT_IGNORED,      'F', NULL,              ARGS_NONE },
    { OPT_IGNORED,      'L', "logical",         ARGS_NONE },
    { OPT_IGNORED,      'P', "physical",        ARGS_NONE },
    { OPT_FORCE,        'f', "force",           ARGS_NONE },
    { OPT_INTERACTIVE,  'i', "interactive",     ARGS_NONE },
    { OPT_NODEREF,      'n', "no-dereference",  ARGS_NONE },
    { OPT_RELATIVE,     'r', "relative",        ARGS_NONE },
    { OPT_SYMBOLIC,     's', "symbolic",        ARGS_NONE },
    { OPT_SUFFIX,       'S', "suffix",          ARGS_REQUIRED },
    { OPT_TARGET_DIR,   't', "target-directory", ARGS_REQUIRED },
    { OPT_NO_TARGET_DIR, 'T', "no-target-directory", ARGS_NONE },
    { OPT_VERBOSE,      'v', "verbose",         ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int         backup, force, interactive, noDereference, relative, symbolic, noTargetDirectory, verbose;
    const char* suffix;
    const char* targetDirectory;
} Settings;

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;

    switch (id) {
        case OPT_BACKUP_SHORT: s->backup = 1; break;
        case OPT_BACKUP:
            if (value != NULL && strcmp(value, "none") == 0 || (value != NULL && strcmp(value, "off") == 0))
                s->backup = 0;
            else
                s->backup = 1;
            break;
        case OPT_FORCE:        s->force = 1; s->interactive = 0; break;
        case OPT_INTERACTIVE:  s->interactive = 1; s->force = 0; break;
        case OPT_NODEREF:      s->noDereference = 1; break;
        case OPT_RELATIVE:     s->relative = 1; break;
        case OPT_SYMBOLIC:     s->symbolic = 1; break;
        case OPT_SUFFIX:       s->suffix = value; break;
        case OPT_TARGET_DIR:   s->targetDirectory = value; break;
        case OPT_NO_TARGET_DIR: s->noTargetDirectory = 1; break;
        case OPT_VERBOSE:      s->verbose = 1; break;
    }
    return 0;
}

/* ---- relative paths -------------------------------------------------------------------------- */

/** The path of `to` as seen from the directory `from` (both absolute and virtual, without . and ..). */
static void relative_path(const char* from, const char* to, char* out, size_t size) {
    size_t i = 0, common = 0, ups = 0;
    const char* p;

    // the length of the common directories
    while (from[i] != '\0' && to[i] != '\0' && tolower((unsigned char)from[i]) == tolower((unsigned char)to[i])) {
        if (from[i] == '/' && (from[i + 1] == '\0' || to[i + 1] != '\0' || 1))
            common = i;
        i++;
    }
    if (from[i] == '\0' && (to[i] == '/' || to[i] == '\0'))
        common = i;
    else if (to[i] == '\0' && from[i] == '/')
        common = i;
    for (p = from + common; *p != '\0'; p++)
        if (*p == '/' && p[1] != '\0')
            ups++;
    out[0] = '\0';
    while (ups-- > 0)
        strncat(out, "../", size - strlen(out) - 1);
    p = to + common;
    while (*p == '/')
        p++;
    strncat(out, p, size - strlen(out) - 1);
    if (out[0] == '\0')
        snprintf(out, size, ".");
}

/** The absolute virtual path of a path as written (relative to the current directory). */
static int absolute_virtual(const char* path, char* out, size_t size) {
    char cwd[VPATH_MAX];

    if (vpath_getcwd(cwd, sizeof cwd) != 0)
        return -1;
    return vpath_resolve(cwd, path, out, size);
}

/* ---- links ----------------------------------------------------------------------------------- */

typedef struct {
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    USHORT SubstituteNameOffset, SubstituteNameLength, PrintNameOffset, PrintNameLength;
    WCHAR  PathBuffer[MAX_PATH * 2 + 8];
} MountPointBuffer;

/** Makes a junction at `link` (a directory that does not exist yet) to the directory `target` (native, absolute). */
static int make_junction(const char* link, const char* target) {
    WCHAR wide[MAX_PATH + 8];
    MountPointBuffer buffer;
    HANDLE directory;
    DWORD got = 0, length;
    size_t substituteBytes, printBytes;

    if (MultiByteToWideChar(CP_UTF8, 0, target, -1, wide, MAX_PATH) == 0)
        return -1;
    if (!CreateDirectoryA(link, NULL))
        return -1;
    memset(&buffer, 0, sizeof buffer);
    // the substitute name is \??\C:\dir, the print name C:\dir
    substituteBytes = (wcslen(wide) + 4) * sizeof(WCHAR);
    printBytes = wcslen(wide) * sizeof(WCHAR);
    buffer.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    buffer.SubstituteNameOffset = 0;
    buffer.SubstituteNameLength = (USHORT)substituteBytes;
    buffer.PrintNameOffset = (USHORT)(substituteBytes + sizeof(WCHAR));
    buffer.PrintNameLength = (USHORT)printBytes;
    wcscpy(buffer.PathBuffer, L"\\??\\");
    wcscat(buffer.PathBuffer, wide);
    wcscpy(buffer.PathBuffer + substituteBytes / sizeof(WCHAR) + 1, wide);
    length = (DWORD)(8 + substituteBytes + sizeof(WCHAR) + printBytes + sizeof(WCHAR));
    buffer.ReparseDataLength = (USHORT)length;
    directory = CreateFileA(link, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (directory == INVALID_HANDLE_VALUE) {
        RemoveDirectoryA(link);
        return -1;
    }
    if (!DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, &buffer, length + 8, NULL, 0, &got, NULL)) {
        DWORD error = GetLastError();

        CloseHandle(directory);
        RemoveDirectoryA(link);
        SetLastError(error);
        return -1;
    }
    CloseHandle(directory);
    return 0;
}

/** The text a symbolic link stores for the target as written: a virtual absolute path becomes a Windows one. */
static void link_text(const char* target, char* out, size_t size) {
    char native[VPATH_MAX];

    if (vpath_is_separator(target[0]) && vpath_to_native(target, native, sizeof native) == 0) {
        snprintf(out, size, "%s", native);
    } else {
        char* p;

        snprintf(out, size, "%s", target);
        for (p = out; *p != '\0'; p++)
            if (*p == '/')
                *p = '\\';
    }
}

static int make_symbolic(const char* nativeLink, const char* target) {
    char text[VPATH_MAX * 2], resolved[VPATH_MAX * 2], directory[VPATH_MAX];
    DWORD attributes, flags = 2;       /* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE */
    int targetIsDirectory = 0;
    char* slash;

    link_text(target, text, sizeof text);
    // does the target exist, and is it a directory? (a relative one is seen from the directory of the link)
    snprintf(directory, sizeof directory, "%s", nativeLink);
    slash = strrchr(directory, '\\');
    if (slash != NULL)
        *slash = '\0';
    else
        directory[0] = '\0';
    if (text[0] != '\0' && (text[1] == ':' || text[0] == '\\'))
        snprintf(resolved, sizeof resolved, "%s", text);
    else
        snprintf(resolved, sizeof resolved, "%s%s%s", directory, directory[0] != '\0' ? "\\" : "", text);
    if (fs_attributes(resolved, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY))
        targetIsDirectory = 1;

    if (CreateSymbolicLinkA(nativeLink, text, flags | (targetIsDirectory ? 1 : 0)))
        return 0;
    if (targetIsDirectory && (GetLastError() == ERROR_PRIVILEGE_NOT_HELD || GetLastError() == ERROR_INVALID_PARAMETER)) {
        char full[MAX_PATH * 2];

        if (GetFullPathNameA(resolved, sizeof full, full, NULL) > 0 && make_junction(nativeLink, full) == 0)
            return 0;
    }
    if (GetLastError() == ERROR_PRIVILEGE_NOT_HELD)
        SetLastError(ERROR_ACCESS_DENIED);
    return -1;
}

/* ---- the operands ---------------------------------------------------------------------------- */

static int is_directory_path(const char* native, int dereference) {
    DWORD attributes;

    if (fs_attributes(native, &attributes) != 0 || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    if (!dereference && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return 0;
    return 1;
}

/** Makes one link; returns 0, or 1 after a message. */
static int link_one(const Settings* s, const char* target, const char* name) {
    char nativeName[VPATH_MAX], nativeTarget[VPATH_MAX];
    DWORD attributes;
    int exists;
    const char* stored = target;
    char relative[VPATH_MAX * 2];

    if (vpath_to_native(name, nativeName, sizeof nativeName) != 0) {
        fprintf(stderr, PROGRAM ": failed to create %s link '%s': Invalid argument\n", s->symbolic ? "symbolic" : "hard", name);
        return 1;
    }
    exists = fs_attributes(nativeName, &attributes) == 0;
    // a dangling symbolic link exists as well
    if (!exists && GetFileAttributesA(nativeName) != INVALID_FILE_ATTRIBUTES)
        exists = 1;

    if (exists && !s->symbolic && vpath_to_native(target, nativeTarget, sizeof nativeTarget) == 0 &&
        fs_same_file(nativeTarget, nativeName)) {
        fprintf(stderr, PROGRAM ": '%s' and '%s' are the same file\n", name, target);
        return 1;
    }
    if (exists) {
        if (s->interactive && !s->force) {
            if (!fs_confirm(PROGRAM ": replace '%s'? ", name))
                return 0;
        } else if (!s->force && !s->backup) {
            if (s->symbolic)
                fprintf(stderr, PROGRAM ": failed to create symbolic link '%s': File exists\n", name);
            else
                fprintf(stderr, PROGRAM ": failed to create hard link '%s' => '%s': File exists\n", name, target);
            return 1;
        }
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            fprintf(stderr, PROGRAM ": cannot overwrite directory '%s'\n", name);
            return 1;
        }
        if (s->backup) {
            char backup[VPATH_MAX * 2];
            const char* suffix = s->suffix != NULL ? s->suffix : "~";

            snprintf(backup, sizeof backup, "%s%s", nativeName, suffix);
            DeleteFileA(backup);
            if (!MoveFileA(nativeName, backup)) {
                fprintf(stderr, PROGRAM ": cannot backup '%s': %s\n", name, fs_strerror(GetLastError()));
                return 1;
            }
        } else {
            if (attributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesA(nativeName, attributes & ~FILE_ATTRIBUTE_READONLY);
            if (!((attributes & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryA(nativeName) : DeleteFileA(nativeName))) {
                fprintf(stderr, PROGRAM ": cannot remove '%s': %s\n", name, fs_strerror(GetLastError()));
                return 1;
            }
        }
    }

    if (s->symbolic) {
        if (s->relative) {
            char absoluteTarget[VPATH_MAX], absoluteName[VPATH_MAX], directory[VPATH_MAX], *slash;

            if (absolute_virtual(target, absoluteTarget, sizeof absoluteTarget) == 0 &&
                absolute_virtual(name, absoluteName, sizeof absoluteName) == 0) {
                snprintf(directory, sizeof directory, "%s", absoluteName);
                slash = strrchr(directory, '/');
                if (slash != NULL && slash != directory)
                    *slash = '\0';
                else if (slash != NULL)
                    slash[1] = '\0';
                relative_path(directory, absoluteTarget, relative, sizeof relative);
                stored = relative;
            }
        }
        if (make_symbolic(nativeName, stored) != 0) {
            fprintf(stderr, PROGRAM ": failed to create symbolic link '%s': %s\n", name, fs_strerror(GetLastError()));
            return 1;
        }
        if (s->verbose)
            printf("'%s' -> '%s'\n", name, stored);
    } else {
        if (vpath_to_native(target, nativeTarget, sizeof nativeTarget) != 0 || fs_attributes(nativeTarget, &attributes) != 0) {
            fprintf(stderr, PROGRAM ": failed to access '%s': No such file or directory\n", target);
            return 1;
        }
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
            fprintf(stderr, PROGRAM ": %s: hard link not allowed for directory\n", target);
            return 1;
        }
        if (!CreateHardLinkA(nativeName, nativeTarget, NULL)) {
            fprintf(stderr, PROGRAM ": failed to create hard link '%s' => '%s': %s\n", name, target, fs_strerror(GetLastError()));
            return 1;
        }
        if (s->verbose)
            printf("'%s' => '%s'\n", name, target);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    Settings s;
    const char** operands;
    int count, result, i, status = 0, targetCount;
    const char* directory = NULL;
    char nativeDirectory[VPATH_MAX];

    _setmode(_fileno(stdout), _O_BINARY);
    memset(&s, 0, sizeof s);
    result = args_parse(&spec, argc, argv, on_option, &s, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing file operand\n");
        args_usage_error(&spec);
        return 1;
    }
    if (s.noTargetDirectory && s.targetDirectory != NULL) {
        fprintf(stderr, PROGRAM ": cannot combine --target-directory and --no-target-directory\n");
        args_usage_error(&spec);
        return 1;
    }

    // which operands are targets, and which is the directory or the name of the link
    if (s.targetDirectory != NULL) {
        directory = s.targetDirectory;
        targetCount = count;
        if (vpath_to_native(directory, nativeDirectory, sizeof nativeDirectory) != 0 || !is_directory_path(nativeDirectory, 1)) {
            fprintf(stderr, PROGRAM ": target '%s' is not a directory\n", directory);
            return 1;
        }
    } else if (s.noTargetDirectory) {
        if (count != 2) {
            if (count == 1)
                fprintf(stderr, PROGRAM ": missing destination file operand after '%s'\n", operands[0]);
            else
                fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[2]);
            args_usage_error(&spec);
            return 1;
        }
        targetCount = 1;
    } else if (count == 1) {
        directory = ".";
        targetCount = 1;
        snprintf(nativeDirectory, sizeof nativeDirectory, ".");
    } else {
        char native[VPATH_MAX];

        directory = operands[count - 1];
        if (vpath_to_native(directory, native, sizeof native) == 0 && is_directory_path(native, !s.noDereference)) {
            snprintf(nativeDirectory, sizeof nativeDirectory, "%s", native);
            targetCount = count - 1;
        } else {
            directory = NULL;
            if (count > 2) {
                fprintf(stderr, PROGRAM ": target '%s' is not a directory\n", operands[count - 1]);
                return 1;
            }
            targetCount = 1;
        }
    }

    for (i = 0; i < targetCount; i++) {
        char name[VPATH_MAX * 2];

        if (directory != NULL) {
            char base[VPATH_MAX];

            fs_basename(operands[i], base, sizeof base);
            if (strcmp(directory, ".") == 0 && count == 1)
                snprintf(name, sizeof name, "%s", base);
            else
                fs_join(directory, base, name, sizeof name);
        } else {
            snprintf(name, sizeof name, "%s", operands[1]);
        }
        if (link_one(&s, operands[i], name) != 0)
            status = 1;
    }
    return fflush(stdout) != 0 ? 1 : status;
}
