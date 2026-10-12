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
       OPT_TARGET };

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
        case OPT_ARCHIVE:     o->fs->recursive = o->fs->preserve = 1; break;
        case OPT_FORCE:       o->fs->force = 1; break;
        case OPT_INTERACTIVE: o->fs->interactive = 1; o->fs->noClobber = 0; break;
        case OPT_NO_CLOBBER:  o->fs->noClobber = 1; o->fs->interactive = 0; break;
        case OPT_PRESERVE:    o->fs->preserve = 1; break;
        case OPT_RECURSIVE:   o->fs->recursive = 1; break;
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
    int count, status = 0, i, result;
    const char* target;
    char targetNative[VPATH_MAX];
    int targetIsDirectory, sourceCount, kind;

    memset(&options, 0, sizeof options);
    options.program = PROGRAM;
    parsed.fs = &options;
    parsed.targetDirectory = NULL;

    result = args_parse(&spec, argc, argv, on_option, &parsed, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    targetDirectory = parsed.targetDirectory;

    if (fs_split_operands(PROGRAM, operands, count, targetDirectory, &target, &sourceCount) != 0)
        return 1;

    kind = vpath_to_native(target, targetNative, sizeof targetNative);
    if (kind == VPATH_ROOT) {
        fprintf(stderr, PROGRAM ": cannot create '%s': Operation not permitted\n", target);
        return 1;
    }
    if (kind != 0) {
        fprintf(stderr, PROGRAM ": cannot create '%s': No such file or directory\n", target);
        return 1;
    }
    targetIsDirectory = fs_target_is_directory(PROGRAM, target, targetNative, targetDirectory != NULL || sourceCount > 1);
    if (targetIsDirectory < 0)
        return 1;

    for (i = 0; i < sourceCount; i++) {
        const char* source = operands[i];
        char sourceNative[VPATH_MAX];
        char dstDisplay[VPATH_MAX], dstNative[VPATH_MAX];

        kind = vpath_to_native(source, sourceNative, sizeof sourceNative);
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

        if (fs_destination(source, target, targetNative, targetIsDirectory, 1,
                           dstDisplay, sizeof dstDisplay, dstNative, sizeof dstNative) != 0) {
            fprintf(stderr, PROGRAM ": cannot copy '%s': Invalid argument\n", source);
            status = 1;
            continue;
        }

        if (fs_copy(&options, source, sourceNative, dstDisplay, dstNative) != 0)
            status = 1;
    }

    free(operands);
    return status;
}
