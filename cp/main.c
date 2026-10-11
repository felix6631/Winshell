#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"
#include "fsutil.h"

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

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

int main(int argc, char* argv[]) {
    FsOptions options;
    const char** operands = malloc((size_t)(argc + 1) * sizeof *operands);
    const char* targetDirectory = NULL;
    int count = 0, optionsEnded = 0, status = 0, i;
    const char* target;
    char targetNative[VPATH_MAX];
    DWORD targetAttributes = 0;
    int targetIsDirectory = 0, targetExists = 0, sourceCount;

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
            } else if (strcmp(arg, "--archive") == 0) {
                options.recursive = options.preserve = 1;
            } else if (strcmp(arg, "--force") == 0) {
                options.force = 1;
            } else if (strcmp(arg, "--interactive") == 0) {
                options.interactive = 1;
                options.noClobber = 0;
            } else if (strcmp(arg, "--no-clobber") == 0) {
                options.noClobber = 1;
                options.interactive = 0;
            } else if (strcmp(arg, "--preserve") == 0) {
                options.preserve = 1;
            } else if (strcmp(arg, "--recursive") == 0) {
                options.recursive = 1;
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
                    case 'a': options.recursive = options.preserve = 1; break;
                    case 'f': options.force = 1; break;
                    case 'i': options.interactive = 1; options.noClobber = 0; break;
                    case 'n': options.noClobber = 1; options.interactive = 0; break;
                    case 'p': options.preserve = 1; break;
                    case 'r':
                    case 'R': options.recursive = 1; break;
                    case 'v': options.verbose = 1; break;
                    case 't':
                        // the directory is the rest of this word, or the next word
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

    // find the target and how many sources there are
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
            fprintf(stderr, PROGRAM ": cannot create '%s': Operation not permitted\n", target);
            return 1;
        }
        if (kind != 0) {
            fprintf(stderr, PROGRAM ": cannot create '%s': No such file or directory\n", target);
            return 1;
        }
    }
    targetExists = fs_attributes(targetNative, &targetAttributes) == 0;
    targetIsDirectory = targetExists && (targetAttributes & FILE_ATTRIBUTE_DIRECTORY);

    if (targetDirectory != NULL && !targetIsDirectory) {
        fprintf(stderr, PROGRAM ": target '%s' is not a directory\n", target);
        return 1;
    }
    if (sourceCount > 1 && !targetIsDirectory) {
        fprintf(stderr, PROGRAM ": target '%s' is not a directory\n", target);
        return 1;
    }

    for (i = 0; i < sourceCount; i++) {
        const char* source = operands[i];
        char sourceNative[VPATH_MAX];
        char dstDisplay[VPATH_MAX], dstNative[VPATH_MAX], name[VPATH_MAX];
        int kind = vpath_to_native(source, sourceNative, sizeof sourceNative);

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

        if (targetIsDirectory) {
            fs_basename(source, name, sizeof name);
            if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                // "cp -r dir/. target" copies the contents: the target itself is the destination
                if (strcmp(name, ".") == 0) {
                    snprintf(dstDisplay, sizeof dstDisplay, "%s", target);
                    snprintf(dstNative, sizeof dstNative, "%s", targetNative);
                } else {
                    fprintf(stderr, PROGRAM ": cannot copy '%s': Invalid argument\n", source);
                    status = 1;
                    continue;
                }
            } else {
                fs_join(target, name, dstDisplay, sizeof dstDisplay);
                fs_join(targetNative, name, dstNative, sizeof dstNative);
                // fs_join writes '/' ; Windows takes it as well, but keep the native form tidy
                {
                    char* c;
                    for (c = dstNative; *c != '\0'; c++)
                        if (*c == '/')
                            *c = '\\';
                }
            }
        } else {
            snprintf(dstDisplay, sizeof dstDisplay, "%s", target);
            snprintf(dstNative, sizeof dstNative, "%s", targetNative);
        }

        if (fs_copy(&options, source, sourceNative, dstDisplay, dstNative) != 0)
            status = 1;
    }

    free(operands);
    return status;
}
