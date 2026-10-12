#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"
#include "fsutil.h"
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "rm"

static void print_usage(void) {
    puts("Usage: rm [OPTION]... FILE...\n"
         "Remove (unlink) the FILE(s).\n"
         "\n"
         "  -f, --force           ignore nonexistent files, never prompt\n"
         "  -i, --interactive     prompt before every removal\n"
         "  -r, -R, --recursive   remove directories and their contents recursively\n"
         "  -d, --dir             remove empty directories\n"
         "  -v, --verbose         explain what is being done\n"
         "      --no-preserve-root  accepted for compatibility; / and the roots of drives\n"
         "                      and shares are never removed recursively\n"
         "      --help            display this help and exit\n"
         "      --version         output version information and exit\n"
         "\n"
         "By default rm does not remove directories; use -r or -d for that.\n"
         "Read-only files are removed too. Only when a person is at the console, and\n"
         "without -f, is the removal of one asked about.\n"
         "FILE names are paths of the virtual file system; wildcards are expanded by the shell.");
}

/** Whether the path ends after n characters, or after one more separator. */
static int ends_after(const char* p, size_t n) {
    if (p[n] == '\0')
        return 1;
    return (p[n] == '\\' || p[n] == '/') && p[n + 1] == '\0';
}

/**
 * Whether the Windows path is the root of a file system: a drive (C:\), a
 * network share (\\server\share) or a volume (\\?\Volume{...}\).
 *
 * The path is made absolute first, so every spelling of it counts: "\" (the
 * root of the current drive), "C:\.", "\\?\C:\" and so on. Removing such a
 * directory recursively is a mistake nobody can undo.
 */
static int is_filesystem_root(const char* native) {
    char full[VPATH_MAX];
    const char* p;
    DWORD length = GetFullPathNameA(native, sizeof full, full, NULL);
    int unc = 0;

    if (length == 0 || length >= sizeof full)
        return 0;
    p = full;

    if (strncmp(p, "\\\\?\\", 4) == 0 || strncmp(p, "\\\\.\\", 4) == 0) {
        p += 4;
        if (_strnicmp(p, "Volume{", 7) == 0) {
            const char* close = strchr(p, '}');
            return close != NULL && ends_after(p, (size_t)(close - p) + 1);
        }
        if (_strnicmp(p, "UNC\\", 4) == 0) {
            p += 4;
            unc = 1;
        }
    } else if (p[0] == '\\' && p[1] == '\\') {
        p += 2;
        unc = 1;
    }

    if (!unc)
        return isalpha((unsigned char)p[0]) && p[1] == ':' && ends_after(p, 2);

    // \\server\share : two names and nothing more
    {
        size_t names = 0;
        const char* q = p;
        while (*q != '\0') {
            size_t n = strcspn(q, "\\/");
            if (n > 0)
                names++;
            q += n;
            while (*q == '\\' || *q == '/')
                q++;
        }
        return names <= 2;
    }
}

enum { OPT_FORCE = 1, OPT_INTERACTIVE, OPT_RECURSIVE, OPT_DIR, OPT_VERBOSE, OPT_ACCEPTED };

static const ArgsOption optionList[] = {
    { OPT_FORCE,       'f', "force",            ARGS_NONE },
    { OPT_INTERACTIVE, 'i', "interactive",      ARGS_NONE },
    { OPT_RECURSIVE,   'r', "recursive",        ARGS_NONE },
    { OPT_RECURSIVE,   'R', NULL,               ARGS_NONE },
    { OPT_DIR,         'd', "dir",              ARGS_NONE },
    { OPT_VERBOSE,     'v', "verbose",          ARGS_NONE },
    // accepted for compatibility; the roots are protected in any case
    { OPT_ACCEPTED,    0,   "no-preserve-root", ARGS_NONE },
    { OPT_ACCEPTED,    0,   "preserve-root",    ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, optionList, 1, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    FsOptions* o = context;
    (void)value;

    switch (id) {
        case OPT_FORCE:       o->force = 1; o->interactive = 0; break;
        case OPT_INTERACTIVE: o->interactive = 1; o->force = 0; break;
        case OPT_RECURSIVE:   o->recursive = 1; break;
        case OPT_DIR:         o->emptyDirs = 1; break;
        case OPT_VERBOSE:     o->verbose = 1; break;
        default:              break;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    FsOptions options;
    const char** operands;
    int operandCount, status = 0, i, result;

    memset(&options, 0, sizeof options);
    options.program = PROGRAM;

    result = args_parse(&spec, argc, argv, on_option, &options, &operands, &operandCount);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    for (i = 0; i < operandCount; i++) {
        const char* arg = operands[i];
        char native[VPATH_MAX], name[VPATH_MAX];
        int kind = vpath_to_native(arg, native, sizeof native);

        // the root of the virtual file system holds the drives; it is nothing that can be removed
        if (kind == VPATH_ROOT) {
            if (options.recursive || options.emptyDirs)
                fprintf(stderr, PROGRAM ": it is dangerous to operate recursively on '%s'\n"
                                PROGRAM ": the root directory is never removed\n", arg);
            else
                fprintf(stderr, PROGRAM ": cannot remove '%s': Is a directory\n", arg);
            status = 1;
            continue;
        }
        if (kind != 0) {
            if (!options.force) {
                fprintf(stderr, PROGRAM ": cannot remove '%s': No such file or directory\n", arg);
                status = 1;
            }
            continue;
        }

        fs_basename(arg, name, sizeof name);
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            fprintf(stderr, PROGRAM ": refusing to remove '.' or '..' directory: skipping '%s'\n", arg);
            status = 1;
            continue;
        }

        // drives, shares and volumes: also in spellings such as \ or \?\C:\ ; --no-preserve-root
        // does not lift this, a mistake here cannot be undone
        if (is_filesystem_root(native)) {
            if (options.recursive || options.emptyDirs) {
                fprintf(stderr, PROGRAM ": it is dangerous to operate recursively on '%s'\n"
                                PROGRAM ": the root of a drive or share is never removed\n", arg);
                status = 1;
                continue;
            }
        }

        if (fs_remove(&options, arg, native) != 0)
            status = 1;
    }

    if (operandCount == 0 && !options.force) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
        return 1;
    }
    return status;
}
