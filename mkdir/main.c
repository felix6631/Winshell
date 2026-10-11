#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"
#include "fsutil.h"

#define VERSION "0.1"
#define PROGRAM "mkdir"

static void print_usage(void) {
    puts("Usage: mkdir [OPTION]... DIRECTORY...\n"
         "Create the DIRECTORY(ies), if they do not already exist.\n"
         "\n"
         "  -m, --mode=MODE   accepted and ignored: Windows has no permission bits\n"
         "  -p, --parents     no error if existing, make parent directories as needed\n"
         "  -v, --verbose     print a message for each created directory\n"
         "      --help        display this help and exit\n"
         "      --version     output version information and exit\n"
         "\n"
         "DIRECTORY names are paths of the virtual file system. The root / holds the\n"
         "drives only: new directories are made below them, not beside them.");
}

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

static int is_separator(char c) {
    return c == '/' || c == '\\';
}

/** The reason to show for a failed CreateDirectory. */
static const char* create_error(DWORD error) {
    switch (error) {
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            return "File exists";
        case ERROR_INVALID_NAME:        // characters such as * ? " < > | which UNIX would take
            return "Invalid argument";
        default:
            return fs_strerror(error);
    }
}

/**
 * The display path with its last `remaining` components cut off: the
 * name a message shows for a parent directory. Works from the end, so it
 * agrees with the Windows path as long as no ".." is involved.
 */
static void display_prefix(const char* display, size_t remaining, char* out, size_t size) {
    size_t length = strlen(display);

    while (length > 1 && is_separator(display[length - 1]))
        length--;
    while (remaining > 0 && length > 0) {
        while (length > 0 && !is_separator(display[length - 1]))
            length--;
        while (length > 1 && is_separator(display[length - 1]))
            length--;
        remaining--;
    }
    if (length >= size)
        length = size - 1;
    memcpy(out, display, length);
    out[length] = '\0';
}

static int directory_exists(const char* native) {
    DWORD attributes;
    return fs_attributes(native, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static int make_directory(const char* display, const char* native, int parents, int verbose) {
    char path[VPATH_MAX];
    size_t length = strlen(native), start = 0, i, components = 0, remaining;

    if (length >= sizeof path) {
        fprintf(stderr, PROGRAM ": cannot create directory '%s': File name too long\n", display);
        return 1;
    }

    if (!parents) {
        if (!CreateDirectoryA(native, NULL)) {
            fprintf(stderr, PROGRAM ": cannot create directory '%s': %s\n", display, create_error(GetLastError()));
            return 1;
        }
        if (verbose)
            printf(PROGRAM ": created directory '%s'\n", display);
        return 0;
    }

    // with -p: every missing component, from the top
    memcpy(path, native, length + 1);
    while (length > 3 && is_separator(path[length - 1]))
        path[--length] = '\0';

    if (length >= 3 && path[1] == ':' && is_separator(path[2])) {
        start = 3;                                  // after the root of a drive
    } else if (path[0] == '\\' && path[1] == '\\') {
        size_t slashes = 0;
        for (start = 2; path[start] != '\0' && slashes < 2; start++)
            if (is_separator(path[start]))
                slashes++;                          // the root is the share: server and share
    }

    for (i = start; i < length; i++)
        if (is_separator(path[i]) && i > start && !is_separator(path[i - 1]))
            components++;
    components++;       // the last one

    remaining = components;
    for (i = start; i <= length; i++) {
        char saved;

        if (i < length && !is_separator(path[i]))
            continue;
        if (i == start || (i > 0 && is_separator(path[i - 1])))
            continue;

        saved = path[i];
        path[i] = '\0';
        remaining--;

        if (!directory_exists(path)) {
            DWORD attributes;
            if (fs_attributes(path, &attributes) == 0) {
                // something that is not a directory is in the way
                fprintf(stderr, PROGRAM ": cannot create directory '%s': File exists\n", display);
                return 1;
            }
            if (!CreateDirectoryA(path, NULL)) {
                char shown[VPATH_MAX];
                display_prefix(display, remaining, shown, sizeof shown);
                fprintf(stderr, PROGRAM ": cannot create directory '%s': %s\n", shown, create_error(GetLastError()));
                return 1;
            }
            if (verbose) {
                char shown[VPATH_MAX];
                display_prefix(display, remaining, shown, sizeof shown);
                printf(PROGRAM ": created directory '%s'\n", shown);
            }
        }
        path[i] = saved;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    int parents = 0, verbose = 0, optionsEnded = 0, operands = 0, status = 0, i;

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
            } else if (strncmp(arg, "--mode=", 7) == 0) {
                // ignored
            } else if (strcmp(arg, "--mode") == 0 && i + 1 < argc) {
                i++;
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
                    case 'm':
                        // the mode is the rest of the word or the next word
                        if (p[1] == '\0') {
                            if (i + 1 < argc) {
                                i++;
                            } else {
                                fprintf(stderr, PROGRAM ": option requires an argument -- 'm'\n");
                                try_help();
                                return 1;
                            }
                        }
                        p += strlen(p) - 1;
                        break;
                    default:
                        fprintf(stderr, PROGRAM ": invalid option -- '%c'\n", *p);
                        try_help();
                        return 1;
                }
            }
        }
    }

    // operands: everything that is not an option, nor the argument of -m
    optionsEnded = 0;
    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];
        char native[VPATH_MAX];
        int kind;

        if (!optionsEnded && strcmp(arg, "--") == 0) {
            optionsEnded = 1;
            continue;
        }
        if (!optionsEnded && arg[0] == '-' && arg[1] != '\0') {
            // skip the argument of -m / --mode
            if (strcmp(arg, "--mode") == 0)
                i++;
            else if (arg[1] != '-' && strchr(arg, 'm') != NULL && arg[strlen(arg) - 1] == 'm')
                i++;
            continue;
        }

        operands++;
        kind = vpath_to_native(arg, native, sizeof native);
        if (kind == VPATH_ROOT) {
            if (parents)
                continue;
            fprintf(stderr, PROGRAM ": cannot create directory '%s': File exists\n", arg);
            status = 1;
            continue;
        }
        if (kind != 0) {
            // the root only has the drives
            fprintf(stderr, PROGRAM ": cannot create directory '%s': Permission denied\n", arg);
            status = 1;
            continue;
        }

        if (make_directory(arg, native, parents, verbose) != 0)
            status = 1;
    }

    if (operands == 0) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        try_help();
        return 1;
    }
    return status;
}
