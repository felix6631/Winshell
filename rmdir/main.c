#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"
#include "fsutil.h"

#define VERSION "0.1"
#define PROGRAM "rmdir"

static void print_usage(void) {
    puts("Usage: rmdir [OPTION]... DIRECTORY...\n"
         "Remove the DIRECTORY(ies), if they are empty.\n"
         "\n"
         "      --ignore-fail-on-non-empty\n"
         "                    ignore each failure that is solely because a directory is non-empty\n"
         "  -p, --parents     remove DIRECTORY and its ancestors; for example, 'rmdir -p a/b'\n"
         "                    is similar to 'rmdir a/b a'\n"
         "  -v, --verbose     output a diagnostic for every directory processed\n"
         "      --help        display this help and exit\n"
         "      --version     output version information and exit\n"
         "\n"
         "DIRECTORY names are paths of the virtual file system. A directory is only\n"
         "removed when it is empty; the read-only attribute does not prevent it, as\n"
         "permissions do not on UNIX.");
}

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

static int is_separator(char c) {
    return c == '/' || c == '\\';
}

/** Cuts the last component off a path. Returns 0 when nothing is left to cut. */
static int strip_last_component(char* path, int keepRoot) {
    size_t length = strlen(path);

    while (length > 1 && is_separator(path[length - 1]))
        path[--length] = '\0';
    while (length > 0 && !is_separator(path[length - 1]))
        length--;
    if (length == 0)
        return 0;
    while (length > 1 && is_separator(path[length - 1]))
        length--;
    if (keepRoot && length == 1 && is_separator(path[0])) {
        path[1] = '\0';
        return 1;
    }
    path[length] = '\0';
    return 1;
}

static int is_drive_root(const char* native) {
    size_t length = strlen(native);
    return (length == 2 && native[1] == ':') ||
           (length == 3 && native[1] == ':' && is_separator(native[2]));
}

/** Removes one directory. Returns 0, or 1 after reporting. */
static int remove_one(const char* display, const char* native, int ignoreNonEmpty, int verbose) {
    DWORD attributes, error;

    if (fs_attributes(native, &attributes) != 0) {
        fprintf(stderr, PROGRAM ": failed to remove '%s': %s\n", display, fs_strerror(GetLastError()));
        return 1;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        fprintf(stderr, PROGRAM ": failed to remove '%s': Not a directory\n", display);
        return 1;
    }

    if (verbose)
        printf(PROGRAM ": removing directory, '%s'\n", display);

    if (attributes & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesA(native, FILE_ATTRIBUTE_NORMAL);

    if (RemoveDirectoryA(native))
        return 0;

    error = GetLastError();
    if (attributes & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesA(native, attributes);     // it stays as it was
    if (error == ERROR_DIR_NOT_EMPTY && ignoreNonEmpty)
        return 0;
    fprintf(stderr, PROGRAM ": failed to remove '%s': %s\n", display, fs_strerror(error));
    return 1;
}

int main(int argc, char* argv[]) {
    int parents = 0, verbose = 0, ignoreNonEmpty = 0, optionsEnded = 0, operands = 0, status = 0, i;

    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (optionsEnded || arg[0] != '-' || arg[1] == '\0') {
            continue;
        } else if (strcmp(arg, "--") == 0) {
            optionsEnded = 1;
        } else if (arg[1] == '-') {
            if (strcmp(arg, "--help") == 0) {
                print_usage();
                return 0;
            } else if (strcmp(arg, "--version") == 0) {
                puts(PROGRAM " (Winshell) " VERSION);
                return 0;
            } else if (strcmp(arg, "--parents") == 0) {
                parents = 1;
            } else if (strcmp(arg, "--verbose") == 0) {
                verbose = 1;
            } else if (strcmp(arg, "--ignore-fail-on-non-empty") == 0) {
                ignoreNonEmpty = 1;
            } else {
                fprintf(stderr, PROGRAM ": unrecognized option '%s'\n", arg);
                try_help();
                return 1;
            }
        } else {
            const char* p;
            for (p = arg + 1; *p != '\0'; p++) {
                switch (*p) {
                    case 'p': parents = 1; break;
                    case 'v': verbose = 1; break;
                    default:
                        fprintf(stderr, PROGRAM ": invalid option -- '%c'\n", *p);
                        try_help();
                        return 1;
                }
            }
        }
    }

    optionsEnded = 0;
    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];
        char native[VPATH_MAX], display[VPATH_MAX];
        int kind;

        if (!optionsEnded && strcmp(arg, "--") == 0) {
            optionsEnded = 1;
            continue;
        }
        if (!optionsEnded && arg[0] == '-' && arg[1] != '\0')
            continue;

        operands++;
        kind = vpath_to_native(arg, native, sizeof native);
        if (kind == VPATH_ROOT) {
            fprintf(stderr, PROGRAM ": failed to remove '%s': Device or resource busy\n", arg);
            status = 1;
            continue;
        }
        if (kind != 0) {
            fprintf(stderr, PROGRAM ": failed to remove '%s': No such file or directory\n", arg);
            status = 1;
            continue;
        }

        if (strlen(arg) >= sizeof display) {
            fprintf(stderr, PROGRAM ": failed to remove '%s': File name too long\n", arg);
            status = 1;
            continue;
        }
        strcpy(display, arg);

        if (remove_one(display, native, ignoreNonEmpty, verbose) != 0) {
            status = 1;
            continue;
        }

        // -p: the parents, one after the other, until something stops it
        while (parents) {
            if (!strip_last_component(native, 0) || !strip_last_component(display, 1))
                break;
            // a drive, or the root, is where it ends
            if (is_drive_root(native) || strcmp(display, "/") == 0 || display[0] == '\0' ||
                strcmp(display, ".") == 0 || strcmp(display, "..") == 0)
                break;
            if (remove_one(display, native, ignoreNonEmpty, verbose) != 0) {
                status = 1;
                break;
            }
        }
    }

    if (operands == 0) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        try_help();
        return 1;
    }
    return status;
}
