#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"
#include "fsutil.h"
#include "args.h"

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

enum { OPT_PARENTS = 1, OPT_VERBOSE, OPT_MODE };

static const ArgsOption optionList[] = {
    { OPT_PARENTS, 'p', "parents", ARGS_NONE },
    { OPT_VERBOSE, 'v', "verbose", ARGS_NONE },
    { OPT_MODE,    'm', "mode",    ARGS_REQUIRED },     // ignored: Windows has no mode bits
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, optionList, 1, 0, 0 };

typedef struct {
    int parents, verbose;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;
    (void)value;

    if (id == OPT_PARENTS)
        o->parents = 1;
    else if (id == OPT_VERBOSE)
        o->verbose = 1;
    return 0;
}

int main(int argc, char* argv[]) {
    Options o = { 0, 0 };
    const char** operands;
    int operandCount, status = 0, i, result;

    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &operandCount);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    for (i = 0; i < operandCount; i++) {
        const char* arg = operands[i];
        char native[VPATH_MAX];
        int kind = vpath_to_native(arg, native, sizeof native);

        if (kind == VPATH_ROOT) {
            if (o.parents)
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

        if (make_directory(arg, native, o.parents, o.verbose) != 0)
            status = 1;
    }

    if (operandCount == 0) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
        return 1;
    }
    return status;
}
