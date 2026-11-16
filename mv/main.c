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
         "  -b                      like --backup but does not accept an argument\n"
         "      --backup[=CONTROL]  make a backup of each existing destination file\n"
         "  -f, --force             do not prompt before overwriting\n"
         "  -i, --interactive       prompt before overwrite\n"
         "  -n, --no-clobber        do not overwrite an existing file\n"
         "  -S, --suffix=SUFFIX     override the usual backup suffix\n"
         "  -t, --target-directory=DIRECTORY  move all SOURCE arguments into DIRECTORY\n"
         "  -T, --no-target-directory  treat DEST as a normal file\n"
         "  -u, --update            move only when the SOURCE file is newer than the destination file\n"
         "      --strip-trailing-slashes  remove any trailing slashes from each SOURCE argument\n"
         "  -v, --verbose           explain what is being done\n"
         "      --help              display this help and exit\n"
         "      --version           output version information and exit\n"
         "\n"
         "Moving to another drive copies and then removes.\n"
         "Paths are those of the virtual file system; wildcards are expanded by the shell.");
}

/** Whether two Windows paths differ only in case, as a rename from a.txt to A.TXT does. */
static int differ_only_in_case(const char* a, const char* b) {
    char fullA[VPATH_MAX], fullB[VPATH_MAX];
    if (GetFullPathNameA(a, sizeof fullA, fullA, NULL) == 0 || GetFullPathNameA(b, sizeof fullB, fullB, NULL) == 0)
        return 0;
    return _stricmp(fullA, fullB) == 0 && strcmp(fullA, fullB) != 0;
}

/**
 * What is at the destination is in the way or not: the same file, a directory into itself, a directory
 * over a file or the reverse are refused; otherwise an existing destination is made free for the move
 * (asking when -i, keeping it with -n). Returns 0 to go on, 1 for an error (reported), 2 to do nothing.
 */
static int prepare_destination(const FsOptions* o, const char* srcDisplay, const char* srcNative,
                               DWORD srcAttributes, const char* dstDisplay, const char* dstNative) {
    DWORD dstAttributes;
    int srcIsDirectory = (srcAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    int dstExists = fs_attributes(dstNative, &dstAttributes) == 0;
    int dstIsDirectory;

    if (dstExists && fs_same_file(srcNative, dstNative) && !differ_only_in_case(srcNative, dstNative)) {
        fprintf(stderr, PROGRAM ": '%s' and '%s' are the same file\n", srcDisplay, dstDisplay);
        return 1;
    }

    if (srcIsDirectory && fs_is_inside(srcNative, dstNative) && !differ_only_in_case(srcNative, dstNative)) {
        fprintf(stderr, PROGRAM ": cannot move '%s' to a subdirectory of itself, '%s'\n", srcDisplay, dstDisplay);
        return 1;
    }

    if (!dstExists || differ_only_in_case(srcNative, dstNative))
        return 0;

    dstIsDirectory = (dstAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (srcIsDirectory && !dstIsDirectory) {
        fprintf(stderr, PROGRAM ": cannot overwrite non-directory '%s' with directory '%s'\n", dstDisplay, srcDisplay);
        return 1;
    }
    if (!srcIsDirectory && dstIsDirectory) {
        fprintf(stderr, PROGRAM ": cannot overwrite directory '%s' with non-directory\n", dstDisplay);
        return 1;
    }
    if (o->noClobber)
        return 2;
    if (o->update && !srcIsDirectory) {
        WIN32_FILE_ATTRIBUTE_DATA src, dst;

        if (GetFileAttributesExA(srcNative, GetFileExInfoStandard, &src) && GetFileAttributesExA(dstNative, GetFileExInfoStandard, &dst) &&
            CompareFileTime(&src.ftLastWriteTime, &dst.ftLastWriteTime) <= 0)
            return 2;
    }
    if (o->interactive && !fs_confirm(PROGRAM ": overwrite '%s'? ", dstDisplay))
        return 2;
    if (o->backup != 0 && !dstIsDirectory && fs_make_backup(o, dstDisplay, dstNative) != 0)
        return 1;

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
            return 2;
        SetFileAttributesA(dstNative, FILE_ATTRIBUTE_NORMAL);
    }
    return 0;
}

/** A move to another drive: copy, then remove what was copied. */
static int move_across_volumes(const FsOptions* o, const char* srcDisplay, const char* srcNative,
                               const char* dstDisplay, const char* dstNative) {
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

static int move_one(const FsOptions* o, const char* srcDisplay, const char* srcNative,
                    const char* dstDisplay, const char* dstNative) {
    DWORD srcAttributes, error;
    int prepared;

    if (fs_attributes(srcNative, &srcAttributes) != 0) {
        fprintf(stderr, PROGRAM ": cannot stat '%s': %s\n", srcDisplay, fs_strerror(GetLastError()));
        return 1;
    }
    fs_backup_reset();
    prepared = prepare_destination(o, srcDisplay, srcNative, srcAttributes, dstDisplay, dstNative);
    if (prepared != 0)
        return prepared == 2 ? 0 : 1;

    if (getenv("WINSHELL_TEST_CROSS_DEVICE") != NULL) {
        // The tests set this to run the code of a move between volumes on a single one:
        // the rename is not tried, and the copy that follows a refused one is done.
        error = ERROR_NOT_SAME_DEVICE;
    } else if (MoveFileExA(srcNative, dstNative, MOVEFILE_REPLACE_EXISTING)) {
        if (o->verbose)
            printf("renamed '%s' -> '%s'%s\n", srcDisplay, dstDisplay, fs_backup_note());
        return 0;
    } else {
        error = GetLastError();
    }

    if (error == ERROR_NOT_SAME_DEVICE)
        return move_across_volumes(o, srcDisplay, srcNative, dstDisplay, dstNative);

    fprintf(stderr, PROGRAM ": cannot move '%s' to '%s': %s\n", srcDisplay, dstDisplay, fs_strerror(error));
    return 1;
}

enum { OPT_FORCE = 1, OPT_INTERACTIVE, OPT_NO_CLOBBER, OPT_VERBOSE, OPT_TARGET, OPT_BACKUP_SHORT, OPT_BACKUP, OPT_SUFFIX,
       OPT_UPDATE, OPT_NO_TARGET_DIRECTORY, OPT_STRIP };

static const ArgsOption optionList[] = {
    { OPT_FORCE,       'f', "force",            ARGS_NONE },
    { OPT_INTERACTIVE, 'i', "interactive",      ARGS_NONE },
    { OPT_NO_CLOBBER,  'n', "no-clobber",       ARGS_NONE },
    { OPT_VERBOSE,     'v', "verbose",          ARGS_NONE },
    { OPT_TARGET,      't', "target-directory", ARGS_REQUIRED },
    { OPT_BACKUP_SHORT, 'b', NULL,              ARGS_NONE },
    { OPT_BACKUP,      0,   "backup",           ARGS_OPTIONAL },
    { OPT_SUFFIX,      'S', "suffix",           ARGS_REQUIRED },
    { OPT_UPDATE,      'u', "update",           ARGS_NONE },
    { OPT_NO_TARGET_DIRECTORY, 'T', "no-target-directory", ARGS_NONE },
    { OPT_STRIP,       0,   "strip-trailing-slashes", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, optionList, 1, 0, 0 };

typedef struct {
    FsOptions*  fs;
    const char* targetDirectory;
    int         noTargetDirectory, strip;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
        case OPT_FORCE:       o->fs->force = 1; o->fs->interactive = o->fs->noClobber = 0; break;
        case OPT_INTERACTIVE: o->fs->interactive = 1; o->fs->force = o->fs->noClobber = 0; break;
        case OPT_NO_CLOBBER:  o->fs->noClobber = 1; o->fs->force = o->fs->interactive = 0; break;
        case OPT_VERBOSE:     o->fs->verbose = 1; break;
        case OPT_TARGET:      o->targetDirectory = value; break;
        case OPT_BACKUP_SHORT: o->fs->backup = 3; break;
        case OPT_BACKUP:
            if (value == NULL) {
                o->fs->backup = 3;
            } else {
                int mode = fs_backup_control(value);

                if (mode < 0) {
                    fprintf(stderr, PROGRAM ": invalid argument '%s' for 'backup type'\nValid arguments are:\n  - 'none', 'off'\n"
                                    "  - 'numbered', 't'\n  - 'existing', 'nil'\n  - 'simple', 'never'\n"
                                    "Try '" PROGRAM " --help' for more information.\n", value);
                    return -1;
                }
                o->fs->backup = mode;
            }
            break;
        case OPT_SUFFIX:      o->fs->suffix = value; if (o->fs->backup == 0) o->fs->backup = 1; break;
        case OPT_UPDATE:      o->fs->update = 1; break;
        case OPT_NO_TARGET_DIRECTORY: o->noTargetDirectory = 1; break;
        case OPT_STRIP:       o->strip = 1; break;
        default: break;
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
    int targetIsDirectory, kind;

    memset(&options, 0, sizeof options);
    options.program = PROGRAM;
    parsed.fs = &options;
    parsed.targetDirectory = NULL;
    parsed.noTargetDirectory = parsed.strip = 0;

    result = args_parse(&spec, argc, argv, on_option, &parsed, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    targetDirectory = parsed.targetDirectory;

    if (parsed.noTargetDirectory && targetDirectory != NULL) {
        fprintf(stderr, PROGRAM ": cannot combine --target-directory (-t) and --no-target-directory (-T)\n");
        return 1;
    }
    if (targetDirectory != NULL) {
        char native[VPATH_MAX];
        DWORD attributes;

        if (vpath_to_native(targetDirectory, native, sizeof native) != 0 || fs_attributes(native, &attributes) != 0) {
            fprintf(stderr, PROGRAM ": failed to access '%s': No such file or directory\n", targetDirectory);
            return 1;
        }
    }
    if (fs_split_operands(PROGRAM, operands, count, targetDirectory, &target, &sourceCount) != 0)
        return 1;
    if (parsed.noTargetDirectory && sourceCount > 1) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\nTry '" PROGRAM " --help' for more information.\n", operands[2]);
        return 1;
    }
    if (target[0] == '\0' && targetDirectory == NULL) {
        fprintf(stderr, PROGRAM ": cannot move '%s' to '': No such file or directory\n", operands[0]);
        return 1;
    }

    kind = vpath_to_native(target, targetNative, sizeof targetNative);
    if (kind == VPATH_ROOT) {
        fprintf(stderr, PROGRAM ": cannot move to '%s': Operation not permitted\n", target);
        return 1;
    }
    if (kind != 0) {
        fprintf(stderr, PROGRAM ": cannot move to '%s': No such file or directory\n", target);
        return 1;
    }
    targetIsDirectory = parsed.noTargetDirectory ? 0
                      : fs_target_is_directory(PROGRAM, target, targetNative, targetDirectory != NULL || sourceCount > 1);
    if (targetIsDirectory < 0)
        return 1;
    if (!targetIsDirectory && !parsed.noTargetDirectory && targetDirectory == NULL && target[0] != '\0' &&
        (target[strlen(target) - 1] == '/' || target[strlen(target) - 1] == '\\')) {
        // a name with a slash at its end is a directory: there is none
        DWORD attributes;

        fprintf(stderr, PROGRAM ": cannot move '%s' to '%s': %s\n", operands[0], target,
                fs_attributes(targetNative, &attributes) == 0 ? "Not a directory" : "No such file or directory");
        return 1;
    }

    for (i = 0; i < sourceCount; i++) {
        const char* source = operands[i];
        char sourceNative[VPATH_MAX];
        char dstDisplay[VPATH_MAX], dstNative[VPATH_MAX];

        char stripped[VPATH_MAX];

        if (parsed.strip) {
            size_t n = strlen(source);

            snprintf(stripped, sizeof stripped, "%s", source);
            while (n > 1 && (stripped[n - 1] == '/' || stripped[n - 1] == '\\'))
                stripped[--n] = '\0';
            source = stripped;
        }
        if (source[0] == '\0') {
            fprintf(stderr, PROGRAM ": cannot stat '': No such file or directory\n");
            status = 1;
            continue;
        }
        kind = vpath_to_native(source, sourceNative, sizeof sourceNative);
        if (kind == 0) {
            DWORD attributes;

            if (fs_attributes(sourceNative, &attributes) != 0) {
                fprintf(stderr, PROGRAM ": cannot stat '%s': %s\n", source, fs_path_error(sourceNative, GetLastError()));
                status = 1;
                continue;
            }
        }
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

        if (parsed.noTargetDirectory) {
            DWORD attributes, sourceAttributes;

            snprintf(dstDisplay, sizeof dstDisplay, "%s", target);
            snprintf(dstNative, sizeof dstNative, "%s", targetNative);
            if (fs_attributes(dstNative, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
                fs_attributes(sourceNative, &sourceAttributes) == 0 && !(sourceAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                fprintf(stderr, PROGRAM ": cannot overwrite directory '%s' with non-directory\n", target);
                status = 1;
                continue;
            }
        } else if (fs_destination(source, target, targetNative, targetIsDirectory, 0,
                           dstDisplay, sizeof dstDisplay, dstNative, sizeof dstNative) != 0) {
            fprintf(stderr, PROGRAM ": cannot move '%s': Invalid argument\n", source);
            status = 1;
            continue;
        }

        {
            // a file in the middle of the destination path
            char parent[VPATH_MAX];
            char* separator;
            DWORD attributes;

            snprintf(parent, sizeof parent, "%s", dstNative);
            separator = strrchr(parent, '\\');
            if (separator != NULL && separator > parent + 2) {
                *separator = '\0';
                if (fs_attributes(parent, &attributes) == 0 && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    fprintf(stderr, PROGRAM ": failed to access '%s': Not a directory\n", dstDisplay);
                    status = 1;
                    continue;
                }
            }
        }
        if (move_one(&options, source, sourceNative, dstDisplay, dstNative) != 0)
            status = 1;
    }

    free(operands);
    return status;
}
