#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"
#include "fsutil.h"
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "mv"

static void print_usage(void) {
    puts("Usage: mv [OPTION]... SOURCE DEST\n"
         "  or:  mv [OPTION]... SOURCE... DIRECTORY\n"
         "  or:  mv [OPTION]... -t DIRECTORY SOURCE...\n"
         "Rename SOURCE to DEST, or move SOURCE(s) to DIRECTORY.\n"
         "\n"
         "  -f, --force             do not prompt before overwriting\n"
         "  -i, --interactive       prompt before overwrite\n"
         "  -n, --no-clobber        do not overwrite an existing file\n"
         "  -t, --target-directory=DIRECTORY  move all SOURCE arguments into DIRECTORY\n"
         "  -v, --verbose           explain what is being done\n"
         "      --help              display this help and exit\n"
         "      --version           output version information and exit\n"
         "\n"
         "Moving to another drive copies and then removes.\n"
         "Paths are those of the virtual file system; wildcards are expanded by the shell.");
}

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

/** Whether two Windows paths differ only in case, as a rename from a.txt to A.TXT does. */
static int differ_only_in_case(const char* a, const char* b) {
    char fullA[VPATH_MAX], fullB[VPATH_MAX];
    if (GetFullPathNameA(a, sizeof fullA, fullA, NULL) == 0 || GetFullPathNameA(b, sizeof fullB, fullB, NULL) == 0)
        return 0;
    return _stricmp(fullA, fullB) == 0 && strcmp(fullA, fullB) != 0;
}

static int move_one(const FsOptions* o, const char* srcDisplay, const char* srcNative,
                    const char* dstDisplay, const char* dstNative) {
    DWORD srcAttributes, dstAttributes;
    int srcIsDirectory, dstExists;
    DWORD error;

    if (fs_attributes(srcNative, &srcAttributes) != 0) {
        fprintf(stderr, PROGRAM ": cannot stat '%s': %s\n", srcDisplay, fs_strerror(GetLastError()));
        return 1;
    }
    srcIsDirectory = (srcAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    dstExists = fs_attributes(dstNative, &dstAttributes) == 0;

    if (dstExists && fs_same_file(srcNative, dstNative) && !differ_only_in_case(srcNative, dstNative)) {
        fprintf(stderr, PROGRAM ": '%s' and '%s' are the same file\n", srcDisplay, dstDisplay);
        return 1;
    }

    if (srcIsDirectory && fs_is_inside(srcNative, dstNative) && !differ_only_in_case(srcNative, dstNative)) {
        fprintf(stderr, PROGRAM ": cannot move '%s' to a subdirectory of itself, '%s'\n", srcDisplay, dstDisplay);
        return 1;
    }

    if (dstExists && !differ_only_in_case(srcNative, dstNative)) {
        int dstIsDirectory = (dstAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (srcIsDirectory && !dstIsDirectory) {
            fprintf(stderr, PROGRAM ": cannot overwrite non-directory '%s' with directory '%s'\n",
                    dstDisplay, srcDisplay);
            return 1;
        }
        if (!srcIsDirectory && dstIsDirectory) {
            fprintf(stderr, PROGRAM ": cannot overwrite directory '%s' with non-directory\n", dstDisplay);
            return 1;
        }
        if (o->noClobber)
            return 0;
        if (o->interactive && !fs_confirm(PROGRAM ": overwrite '%s'? ", dstDisplay))
            return 0;

        if (dstIsDirectory) {
            // an existing directory is replaced only when it is empty
            if (!RemoveDirectoryA(dstNative)) {
                fprintf(stderr, PROGRAM ": cannot move '%s' to '%s': %s\n", srcDisplay, dstDisplay,
                        fs_strerror(GetLastError()));
                return 1;
            }
        } else if (dstAttributes & FILE_ATTRIBUTE_READONLY) {
            if (!o->force && !o->interactive && fs_stdin_is_console() &&
                !fs_confirm(PROGRAM ": replace '%s', overriding the read-only attribute? ", dstDisplay))
                return 0;
            SetFileAttributesA(dstNative, FILE_ATTRIBUTE_NORMAL);
        }
    }

    if (getenv("WINSHELL_TEST_CROSS_DEVICE") != NULL) {
        // The tests set this to run the code of a move between volumes on a single one:
        // the rename is not tried, and the copy that follows a refused one is done.
        error = ERROR_NOT_SAME_DEVICE;
    } else if (MoveFileExA(srcNative, dstNative, MOVEFILE_REPLACE_EXISTING)) {
        if (o->verbose)
            printf("renamed '%s' -> '%s'\n", srcDisplay, dstDisplay);
        return 0;
    } else {
        error = GetLastError();
    }

    if (error == ERROR_NOT_SAME_DEVICE) {
        // another drive: copy, then remove what was copied
        FsOptions copy = *o;
        FsOptions remove = *o;

        copy.recursive = 1;
        copy.preserve = 1;
        copy.force = 1;
        copy.interactive = 0;
        copy.noClobber = 0;
        copy.verbose = 0;
        remove.recursive = 1;
        remove.force = 1;
        remove.interactive = 0;
        remove.verbose = 0;

        if (fs_copy(&copy, srcDisplay, srcNative, dstDisplay, dstNative) != 0) {
            fprintf(stderr, PROGRAM ": cannot move '%s' to '%s': copy failed, the source is left in place\n",
                    srcDisplay, dstDisplay);
            return 1;
        }
        if (fs_remove(&remove, srcDisplay, srcNative) != 0)
            return 1;
        if (o->verbose)
            printf("renamed '%s' -> '%s'\n", srcDisplay, dstDisplay);
        return 0;
    }

    fprintf(stderr, PROGRAM ": cannot move '%s' to '%s': %s\n", srcDisplay, dstDisplay, fs_strerror(error));
    return 1;
}

enum { OPT_FORCE = 1, OPT_INTERACTIVE, OPT_NO_CLOBBER, OPT_VERBOSE, OPT_TARGET };

static const ArgsOption optionList[] = {
    { OPT_FORCE,       'f', "force",            ARGS_NONE },
    { OPT_INTERACTIVE, 'i', "interactive",      ARGS_NONE },
    { OPT_NO_CLOBBER,  'n', "no-clobber",       ARGS_NONE },
    { OPT_VERBOSE,     'v', "verbose",          ARGS_NONE },
    { OPT_TARGET,      't', "target-directory", ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, optionList, 1, 0, 0 };

typedef struct {
    FsOptions*  fs;
    const char* targetDirectory;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
        case OPT_FORCE:       o->fs->force = 1; o->fs->interactive = o->fs->noClobber = 0; break;
        case OPT_INTERACTIVE: o->fs->interactive = 1; o->fs->force = o->fs->noClobber = 0; break;
        case OPT_NO_CLOBBER:  o->fs->noClobber = 1; o->fs->force = o->fs->interactive = 0; break;
        case OPT_VERBOSE:     o->fs->verbose = 1; break;
        case OPT_TARGET:      o->targetDirectory = value; break;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    FsOptions options;
    Options parsed;
    const char** operands;
    const char* targetDirectory;
    int count, status = 0, i, sourceCount, result;
    const char* target;
    char targetNative[VPATH_MAX];
    DWORD targetAttributes = 0;
    int targetIsDirectory;

    memset(&options, 0, sizeof options);
    options.program = PROGRAM;
    parsed.fs = &options;
    parsed.targetDirectory = NULL;

    result = args_parse(&spec, argc, argv, on_option, &parsed, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    targetDirectory = parsed.targetDirectory;

    if (targetDirectory != NULL) {
        target = targetDirectory;
        sourceCount = count;
        if (sourceCount == 0) {
            fprintf(stderr, PROGRAM ": missing file operand\n");
            try_help();
            return 1;
        }
    } else {
        if (count == 0) {
            fprintf(stderr, PROGRAM ": missing file operand\n");
            try_help();
            return 1;
        }
        if (count == 1) {
            fprintf(stderr, PROGRAM ": missing destination file operand after '%s'\n", operands[0]);
            try_help();
            return 1;
        }
        target = operands[count - 1];
        sourceCount = count - 1;
    }

    {
        int kind = vpath_to_native(target, targetNative, sizeof targetNative);
        if (kind == VPATH_ROOT) {
            fprintf(stderr, PROGRAM ": cannot move to '%s': Operation not permitted\n", target);
            return 1;
        }
        if (kind != 0) {
            fprintf(stderr, PROGRAM ": cannot move to '%s': No such file or directory\n", target);
            return 1;
        }
    }
    targetIsDirectory = fs_attributes(targetNative, &targetAttributes) == 0 &&
                        (targetAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if ((targetDirectory != NULL || sourceCount > 1) && !targetIsDirectory) {
        fprintf(stderr, PROGRAM ": target '%s' is not a directory\n", target);
        return 1;
    }

    for (i = 0; i < sourceCount; i++) {
        const char* source = operands[i];
        char sourceNative[VPATH_MAX];
        char dstDisplay[VPATH_MAX], dstNative[VPATH_MAX], name[VPATH_MAX];
        int kind = vpath_to_native(source, sourceNative, sizeof sourceNative);

        if (kind == VPATH_ROOT) {
            fprintf(stderr, PROGRAM ": cannot move '%s': Operation not permitted\n", source);
            status = 1;
            continue;
        }
        if (kind != 0) {
            fprintf(stderr, PROGRAM ": cannot stat '%s': No such file or directory\n", source);
            status = 1;
            continue;
        }

        if (targetIsDirectory) {
            char* c;
            fs_basename(source, name, sizeof name);
            if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                fprintf(stderr, PROGRAM ": cannot move '%s': Invalid argument\n", source);
                status = 1;
                continue;
            }
            fs_join(target, name, dstDisplay, sizeof dstDisplay);
            fs_join(targetNative, name, dstNative, sizeof dstNative);
            for (c = dstNative; *c != '\0'; c++)
                if (*c == '/')
                    *c = '\\';
        } else {
            snprintf(dstDisplay, sizeof dstDisplay, "%s", target);
            snprintf(dstNative, sizeof dstNative, "%s", targetNative);
        }

        if (move_one(&options, source, sourceNative, dstDisplay, dstNative) != 0)
            status = 1;
    }

    free(operands);
    return status;
}
