/*
 * chmod: changes the mode of files, as GNU chmod does: the same modes (octal and symbolic) are computed from the mode the
 * file has. Windows keeps only the write permission, as the read-only attribute of a file: a mode without any
 * write bit (0222) makes the file read-only, one with any makes it writable, and everything else is computed and reported but not
 * kept (a directory keeps nothing). The mode a file has is the one stat shows (0644 or 0444, 0755 for a directory
 * or a program). The messages tell the mode asked for, as GNU chmod does.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "modebits.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "chmod"

static void print_usage(void) {
    puts("Usage: chmod [OPTION]... MODE[,MODE]... FILE...\n"
         "  or:  chmod [OPTION]... OCTAL-MODE FILE...\n"
         "  or:  chmod [OPTION]... --reference=RFILE FILE...\n"
         "Change the mode of each FILE to MODE.\n"
         "With --reference, change the mode of each FILE to that of RFILE.\n"
         "\n"
         "  -c, --changes          like verbose but report only when a change is made\n"
         "  -f, --silent, --quiet  suppress most error messages\n"
         "  -v, --verbose          output a diagnostic for every file processed\n"
         "      --no-preserve-root  do not treat '/' specially (the default)\n"
         "      --preserve-root    fail to operate recursively on '/'\n"
         "      --reference=RFILE  use RFILE's mode instead of MODE values\n"
         "  -R, --recursive        change files and directories recursively\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Each MODE is of the form '[ugoa]*([-+=]([rwxXst]*|[ugo]))+|[-+=][0-7]+'.");
}

enum { OPT_CHANGES = 1, OPT_SILENT, OPT_VERBOSE, OPT_NO_PRESERVE, OPT_PRESERVE, OPT_REFERENCE, OPT_RECURSIVE };

static const ArgsOption options[] = {
    { OPT_CHANGES,     'c', "changes",          ARGS_NONE },
    { OPT_SILENT,      'f', "silent",           ARGS_NONE },
    { OPT_SILENT,      0,   "quiet",            ARGS_NONE },
    { OPT_VERBOSE,     'v', "verbose",          ARGS_NONE },
    { OPT_NO_PRESERVE, 0,   "no-preserve-root", ARGS_NONE },
    { OPT_PRESERVE,    0,   "preserve-root",    ARGS_NONE },
    { OPT_REFERENCE,   0,   "reference",        ARGS_REQUIRED },
    { OPT_RECURSIVE,   'R', "recursive",        ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

static int changesOnly, silent, verbose, recursive, preserveRoot;
static const char* referenceFile;

static int on_option(int id, const char* value, void* context) {
    (void)context;
    switch (id) {
    case OPT_CHANGES: changesOnly = 1; verbose = 0; break;
    case OPT_SILENT: silent = 1; break;
    case OPT_VERBOSE: verbose = 1; changesOnly = 0; break;
    case OPT_NO_PRESERVE: preserveRoot = 0; break;
    case OPT_PRESERVE: preserveRoot = 1; break;
    case OPT_REFERENCE: referenceFile = value; break;
    case OPT_RECURSIVE: recursive = 1; break;
    default: break;
    }
    return 0;
}

static const char* quote(const char* text) {
    static char buffer[4][2048];
    static int which;
    char* out = buffer[which++ & 3];
    size_t n = 0;
    const unsigned char* p;

    out[n++] = '\'';
    for (p = (const unsigned char*)text; *p != '\0' && n < 2048 - 8; p++) {
        if (*p == '\'') {
            out[n++] = '\'';
            out[n++] = '\\';
            out[n++] = '\'';
            out[n++] = '\'';
        } else {
            out[n++] = (char)*p;
        }
    }
    out[n++] = '\'';
    out[n] = '\0';
    return out;
}

typedef struct {
    DWORD attributes;
    int   isLink;
    int   isDirectory;
} Info;

/** The attributes of a file; a link is looked at itself unless follow. Returns 0, or -1 with the error. */
static int read_info(const char* native, int follow, Info* info, DWORD* error) {
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    HANDLE h;
    BY_HANDLE_FILE_INFORMATION data;

    if (!follow)
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    h = CreateFileA(native, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, flags, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return -1;
    }
    if (!GetFileInformationByHandle(h, &data)) {
        *error = GetLastError();
        CloseHandle(h);
        return -1;
    }
    CloseHandle(h);
    info->attributes = data.dwFileAttributes;
    info->isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info->isLink = !follow && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    return 0;
}

static ModeChange* changes;
static unsigned referenceMode;
static int ok = 1;

static void describe(const char* name, int failed, unsigned oldMode, unsigned newMode) {
    char oldText[16], newText[16];

    mode_text9(oldMode, oldText);
    mode_text9(newMode, newText);
    if (failed) {
        printf("failed to change mode of %s from %04o (%s) to %04o (%s)\n", quote(name), oldMode, oldText, newMode, newText);
    } else if (oldMode != newMode) {
        printf("mode of %s changed from %04o (%s) to %04o (%s)\n", quote(name), oldMode, oldText, newMode, newText);
    } else {
        printf("mode of %s retained as %04o (%s)\n", quote(name), oldMode, oldText);
    }
}

static void process(const char* name, const char* native, int commandLine);

static void process_children(const char* name, const char* native) {
    char pattern[VPATH_MAX];
    WIN32_FIND_DATAA data;
    HANDLE find;

    snprintf(pattern, sizeof pattern, "%s\\*", native);
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        char childName[VPATH_MAX], childNative[VPATH_MAX];

        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
            continue;
        fs_join(name, data.cFileName, childName, sizeof childName);
        fs_join(native, data.cFileName, childNative, sizeof childNative);
        process(childName, childNative, 0);
    } while (FindNextFileA(find, &data));
    FindClose(find);
}

static void process(const char* name, const char* native, int commandLine) {
    Info info;
    DWORD error = 0;
    unsigned oldMode, newMode;
    int failed = 0;

    if (read_info(native, commandLine, &info, &error) != 0) {
        if (!silent)
            fprintf(stderr, PROGRAM ": cannot access %s: %s\n", quote(name), fs_strerror(error));
        ok = 0;
        if (verbose)
            describe(name, 1, 0, 0);
        return;
    }
    if (info.isLink)
        return;                                   // a link met on the way is left alone
    oldMode = mode_of_attributes(info.attributes, name) & 07777;
    newMode = referenceFile != NULL ? referenceMode : mode_adjust(oldMode, info.isDirectory, 022, changes);
    if (!info.isDirectory) {
        int wantReadOnly = (newMode & 0222) == 0;
        int isReadOnly = (info.attributes & FILE_ATTRIBUTE_READONLY) != 0;

        if (wantReadOnly != isReadOnly) {
            DWORD attributes = wantReadOnly ? info.attributes | FILE_ATTRIBUTE_READONLY : info.attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY;

            if (attributes == 0)
                attributes = FILE_ATTRIBUTE_NORMAL;
            if (!SetFileAttributesA(native, attributes)) {
                if (!silent)
                    fprintf(stderr, PROGRAM ": changing permissions of %s: %s\n", quote(name), fs_strerror(GetLastError()));
                ok = 0;
                failed = 1;
            }
        }
    }
    if (failed) {
        if (verbose || changesOnly)
            describe(name, 1, oldMode, newMode);
    } else if (verbose || (changesOnly && oldMode != newMode)) {
        describe(name, 0, oldMode, newMode);
    }
    if (recursive && info.isDirectory)
        process_children(name, native);
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, i, modeIndex = -1, newArgc = 0, first = 0;
    char** newArgv;
    const char* modeText = NULL;

    _setmode(_fileno(stdout), _O_BINARY);
    // a mode such as -w or -rwx is not an option: take it out before the options are read
    newArgv = malloc(((size_t)argc + 1) * sizeof *newArgv);
    for (i = 0; i < argc; i++) {
        const char* a = argv[i];

        if (i > 0 && modeIndex < 0 && a[0] == '-' && a[1] != '\0' && strchr("rwxXstugoa", a[1]) != NULL && a[1] != '-') {
            modeIndex = i;
            continue;
        }
        if (strcmp(a, "--") == 0 && i > 0) {
            while (i < argc)
                newArgv[newArgc++] = argv[i++];
            break;
        }
        newArgv[newArgc++] = argv[i];
    }
    newArgv[newArgc] = NULL;
    result = args_parse(&spec, newArgc, newArgv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (referenceFile == NULL) {
        if (modeIndex >= 0) {
            modeText = argv[modeIndex];
        } else if (count > 0) {
            modeText = operands[0];
            first = 1;
        }
    }
    if (count - first <= 0 && !(referenceFile == NULL && modeText == NULL && count == 0)) {
        if (modeText != NULL && referenceFile == NULL) {
            fprintf(stderr, PROGRAM ": missing operand after %s\n", quote(modeText));
        } else if (referenceFile != NULL) {
            fprintf(stderr, PROGRAM ": missing operand\n");
        } else {
            fprintf(stderr, PROGRAM ": missing operand\n");
        }
        args_usage_error(&spec);
        return 1;
    }
    if (count - first <= 0 && modeText == NULL) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        args_usage_error(&spec);
        return 1;
    }
    if (referenceFile != NULL) {
        char native[VPATH_MAX];
        Info info;
        DWORD error = 0;

        if (vpath_to_native(referenceFile, native, sizeof native) != 0 || read_info(native, 1, &info, &error) != 0) {
            fprintf(stderr, PROGRAM ": failed to get attributes of %s: %s\n", quote(referenceFile),
                    fs_strerror(error != 0 ? error : ERROR_FILE_NOT_FOUND));
            return 1;
        }
        referenceMode = mode_of_attributes(info.attributes, referenceFile) & 07777;
    } else {
        changes = mode_compile(modeText);
        if (changes == NULL) {
            fprintf(stderr, PROGRAM ": invalid mode: %s\n", quote(modeText));
            args_usage_error(&spec);
            return 1;
        }
    }
    for (i = first; i < count; i++) {
        char native[VPATH_MAX];

        if (preserveRoot && recursive && (strcmp(operands[i], "/") == 0)) {
            fprintf(stderr, PROGRAM ": it is dangerous to operate recursively on '/'\n");
            fprintf(stderr, PROGRAM ": use --no-preserve-root to override this failsafe\n");
            ok = 0;
            continue;
        }
        if (vpath_to_native(operands[i], native, sizeof native) != 0) {
            if (!silent)
                fprintf(stderr, PROGRAM ": cannot access %s: No such file or directory\n", quote(operands[i]));
            ok = 0;
            if (verbose)
                describe(operands[i], 1, 0, 0);
            continue;
        }
        process(operands[i], native, 1);
    }
    return ok ? 0 : 1;
}
