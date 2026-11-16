#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"
#include "fsutil.h"
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "cp"

static void print_usage(void) {
    puts("Usage: cp [OPTION]... SOURCE DEST\n"
         "  or:  cp [OPTION]... SOURCE... DIRECTORY\n"
         "  or:  cp [OPTION]... -t DIRECTORY SOURCE...\n"
         "Copy SOURCE to DEST, or multiple SOURCE(s) to DIRECTORY.\n"
         "\n"
         "  -a, --archive           same as -rp\n"
         "  -b                      like --backup but does not accept an argument\n"
         "      --backup[=CONTROL]  make a backup of each existing destination file\n"
         "  -d                      accepted: links are not followed on Windows\n"
         "  -l, --link              accepted and ignored: the files are copied\n"
         "  -S, --suffix=SUFFIX     override the usual backup suffix\n"
         "  -T, --no-target-directory  treat DEST as a normal file\n"
         "  -u, --update            copy only when the SOURCE file is newer than the destination file\n"
         "      --parents           use full source file name under DIRECTORY\n"
         "      --strip-trailing-slashes  remove any trailing slashes from each SOURCE argument\n"
         "  -f, --force             if an existing destination file cannot be opened, remove it and try again\n"
         "  -i, --interactive       prompt before overwrite\n"
         "  -n, --no-clobber        do not overwrite an existing file\n"
         "  -p, --preserve          preserve modification times and attributes\n"
         "  -r, -R, --recursive     copy directories recursively\n"
         "  -t, --target-directory=DIRECTORY  copy all SOURCE arguments into DIRECTORY\n"
         "  -v, --verbose           explain what is being done\n"
         "      --help              display this help and exit\n"
         "      --version           output version information and exit\n"
         "\n"
         "Paths are those of the virtual file system; wildcards are expanded by the shell.");
}

enum { OPT_ARCHIVE = 1, OPT_FORCE, OPT_INTERACTIVE, OPT_NO_CLOBBER, OPT_PRESERVE, OPT_RECURSIVE, OPT_VERBOSE,
       OPT_TARGET, OPT_BACKUP_SHORT, OPT_BACKUP, OPT_SUFFIX, OPT_UPDATE, OPT_NO_TARGET_DIRECTORY, OPT_PARENTS,
       OPT_STRIP, OPT_IGNORED };

static const ArgsOption optionList[] = {
    { OPT_ARCHIVE,     'a', "archive",          ARGS_NONE },
    { OPT_FORCE,       'f', "force",            ARGS_NONE },
    { OPT_INTERACTIVE, 'i', "interactive",      ARGS_NONE },
    { OPT_NO_CLOBBER,  'n', "no-clobber",       ARGS_NONE },
    { OPT_PRESERVE,    'p', "preserve",         ARGS_NONE },
    { OPT_RECURSIVE,   'r', "recursive",        ARGS_NONE },
    { OPT_RECURSIVE,   'R', NULL,               ARGS_NONE },
    { OPT_VERBOSE,     'v', "verbose",          ARGS_NONE },
    { OPT_TARGET,      't', "target-directory", ARGS_REQUIRED },
    { OPT_BACKUP_SHORT, 'b', NULL,              ARGS_NONE },
    { OPT_BACKUP,      0,   "backup",           ARGS_OPTIONAL },
    { OPT_SUFFIX,      'S', "suffix",           ARGS_REQUIRED },
    { OPT_UPDATE,      'u', "update",           ARGS_NONE },
    { OPT_NO_TARGET_DIRECTORY, 'T', "no-target-directory", ARGS_NONE },
    { OPT_PARENTS,     0,   "parents",          ARGS_NONE },
    { OPT_STRIP,       0,   "strip-trailing-slashes", ARGS_NONE },
    { OPT_IGNORED,     'd', NULL,               ARGS_NONE },
    { OPT_IGNORED,     'l', "link",             ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, optionList, 1, 0, 0 };

typedef struct {
    FsOptions*  fs;
    const char* targetDirectory;
    int         noTargetDirectory, parents, strip;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
        case OPT_ARCHIVE:     o->fs->recursive = o->fs->preserve = 1; break;
        case OPT_FORCE:       o->fs->force = 1; break;
        case OPT_INTERACTIVE: o->fs->interactive = 1; o->fs->noClobber = 0; break;
        case OPT_NO_CLOBBER:  o->fs->noClobber = 1; o->fs->interactive = 0; break;
        case OPT_PRESERVE:    o->fs->preserve = 1; break;
        case OPT_RECURSIVE:   o->fs->recursive = 1; break;
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
        case OPT_PARENTS:     o->parents = 1; break;
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
    int count, status = 0, i, result;
    const char* target;
    char targetNative[VPATH_MAX];
    int targetIsDirectory, sourceCount, kind;

    memset(&options, 0, sizeof options);
    options.program = PROGRAM;
    parsed.fs = &options;
    parsed.targetDirectory = NULL;
    parsed.noTargetDirectory = parsed.parents = parsed.strip = 0;

    result = args_parse(&spec, argc, argv, on_option, &parsed, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    targetDirectory = parsed.targetDirectory;

    if (parsed.noTargetDirectory && targetDirectory != NULL) {
        fprintf(stderr, PROGRAM ": cannot combine --target-directory (-t) and --no-target-directory (-T)\n");
        return 1;
    }
    if (targetDirectory != NULL) {
        // -t DIRECTORY has to be one
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
        fprintf(stderr, PROGRAM ": cannot create regular file '': No such file or directory\n");
        return 1;
    }
    kind = vpath_to_native(target, targetNative, sizeof targetNative);
    if (kind == VPATH_ROOT) {
        fprintf(stderr, PROGRAM ": cannot create '%s': Operation not permitted\n", target);
        return 1;
    }
    if (kind != 0) {
        fprintf(stderr, PROGRAM ": cannot create '%s': No such file or directory\n", target);
        return 1;
    }
    targetIsDirectory = parsed.noTargetDirectory ? 0
                      : fs_target_is_directory(PROGRAM, target, targetNative, targetDirectory != NULL || sourceCount > 1);
    if (targetIsDirectory < 0)
        return 1;

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
            fprintf(stderr, PROGRAM ": cannot copy '%s': Operation not permitted\n", source);
            status = 1;
            continue;
        }
        if (kind != 0) {
            fprintf(stderr, PROGRAM ": cannot stat '%s': No such file or directory\n", source);
            status = 1;
            continue;
        }

        if (parsed.parents) {
            // --parents: the whole name of the source below the directory, its own directories made first
            const char* name = source;
            char directory[VPATH_MAX], made[VPATH_MAX];
            size_t k;

            while (name[0] == '.' && (name[1] == '/' || name[1] == '\\'))
                name += 2;
            while (name[0] == '/')
                name++;
            fs_join(target, name, dstDisplay, sizeof dstDisplay);
            if (vpath_to_native(dstDisplay, dstNative, sizeof dstNative) != 0) {
                status = 1;
                continue;
            }
            snprintf(directory, sizeof directory, "%s", dstNative);
            for (k = 3; directory[k] != '\0'; k++) {
                if (directory[k] == '\\') {
                    memcpy(made, directory, k);
                    made[k] = '\0';
                    CreateDirectoryA(made, NULL);
                }
            }
        } else if (parsed.noTargetDirectory) {
            DWORD attributes;

            snprintf(dstDisplay, sizeof dstDisplay, "%s", target);
            snprintf(dstNative, sizeof dstNative, "%s", targetNative);
            if (fs_attributes(dstNative, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                DWORD sourceAttributes;

                if (fs_attributes(sourceNative, &sourceAttributes) == 0 && !(sourceAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    fprintf(stderr, PROGRAM ": cannot overwrite directory '%s' with non-directory\n", target);
                    status = 1;
                    continue;
                }
            }
        } else if (fs_destination(source, target, targetNative, targetIsDirectory, 1,
                           dstDisplay, sizeof dstDisplay, dstNative, sizeof dstNative) != 0) {
            fprintf(stderr, PROGRAM ": cannot copy '%s': Invalid argument\n", source);
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
        if (dstDisplay[0] == '\0') {
            fprintf(stderr, PROGRAM ": cannot create regular file '': No such file or directory\n");
            status = 1;
            continue;
        }
        if (fs_copy(&options, source, sourceNative, dstDisplay, dstNative) != 0)
            status = 1;
    }

    free(operands);
    return status;
}
