#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"
#include "fsutil.h"

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

    if (MoveFileExA(srcNative, dstNative, MOVEFILE_REPLACE_EXISTING)) {
        if (o->verbose)
            printf("renamed '%s' -> '%s'\n", srcDisplay, dstDisplay);
        return 0;
    }

    error = GetLastError();
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

int main(int argc, char* argv[]) {
    FsOptions options;
    const char** operands = malloc((size_t)(argc + 1) * sizeof *operands);
    const char* targetDirectory = NULL;
    int count = 0, optionsEnded = 0, status = 0, i, sourceCount;
    const char* target;
    char targetNative[VPATH_MAX];
    DWORD targetAttributes = 0;
    int targetIsDirectory;

    if (operands == NULL)
        return 1;
    memset(&options, 0, sizeof options);
    options.program = PROGRAM;

    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (optionsEnded || arg[0] != '-' || arg[1] == '\0') {
            operands[count++] = arg;
        } else if (strcmp(arg, "--") == 0) {
            optionsEnded = 1;
        } else if (arg[1] == '-') {
            if (strcmp(arg, "--help") == 0) {
                print_usage();
                return 0;
            } else if (strcmp(arg, "--version") == 0) {
                puts(PROGRAM " (Winshell) " VERSION);
                return 0;
            } else if (strcmp(arg, "--force") == 0) {
                options.force = 1;
                options.interactive = options.noClobber = 0;
            } else if (strcmp(arg, "--interactive") == 0) {
                options.interactive = 1;
                options.force = options.noClobber = 0;
            } else if (strcmp(arg, "--no-clobber") == 0) {
                options.noClobber = 1;
                options.force = options.interactive = 0;
            } else if (strcmp(arg, "--verbose") == 0) {
                options.verbose = 1;
            } else if (strncmp(arg, "--target-directory=", 19) == 0) {
                targetDirectory = arg + 19;
            } else if (strcmp(arg, "--target-directory") == 0 && i + 1 < argc) {
                targetDirectory = argv[++i];
            } else {
                fprintf(stderr, PROGRAM ": unrecognized option '%s'\n", arg);
                try_help();
                return 1;
            }
        } else {
            const char* p;
            for (p = arg + 1; *p != '\0'; p++) {
                switch (*p) {
                    case 'f': options.force = 1; options.interactive = options.noClobber = 0; break;
                    case 'i': options.interactive = 1; options.force = options.noClobber = 0; break;
                    case 'n': options.noClobber = 1; options.force = options.interactive = 0; break;
                    case 'v': options.verbose = 1; break;
                    case 't':
                        if (p[1] != '\0') {
                            targetDirectory = p + 1;
                            p += strlen(p) - 1;
                        } else if (i + 1 < argc) {
                            targetDirectory = argv[++i];
                        } else {
                            fprintf(stderr, PROGRAM ": option requires an argument -- 't'\n");
                            try_help();
                            return 1;
                        }
                        break;
                    default:
                        fprintf(stderr, PROGRAM ": invalid option -- '%c'\n", *p);
                        try_help();
                        return 1;
                }
            }
        }
    }

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
