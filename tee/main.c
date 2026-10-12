#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "textutil.h"
#include "fsutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "tee"

static void print_usage(void) {
    puts("Usage: tee [OPTION]... [FILE]...\n"
         "Copy standard input to each FILE, and also to standard output.\n"
         "\n"
         "  -a, --append              append to the given FILEs, do not overwrite\n"
         "  -i, --ignore-interrupts   (accepted; Ctrl+C always ends the program)\n"
         "  -p                        (accepted, same as --output-error=warn)\n"
         "      --output-error[=MODE] (accepted)\n"
         "      --help                display this help and exit\n"
         "      --version             output version information and exit\n"
         "\n"
         "The data is passed on as it arrives, so tee can sit in a pipe that goes on\n"
         "for a long time. A FILE that cannot be opened is reported and the others still\n"
         "get the data; the exit status is then 1. FILE is a path of the virtual file\n"
         "system; \"-\" is a file named -.");
}

int main(int argc, char* argv[]) {
    FILE** files;
    const char** names;
    size_t fileCount = 0, i;
    int append = 0, optionsEnded = 0, status = 0, a;
    unsigned char buffer[65536];

    files = calloc((size_t)argc, sizeof *files);
    names = calloc((size_t)argc, sizeof *names);
    if (files == NULL || names == NULL) {
        fprintf(stderr, PROGRAM ": out of memory\n");
        return 1;
    }

    for (a = 1; a < argc; a++) {
        const char* arg = argv[a];

        if (optionsEnded || arg[0] != '-' || arg[1] == '\0') {
            names[fileCount++] = arg;
        } else if (strcmp(arg, "--") == 0) {
            optionsEnded = 1;
        } else if (arg[1] == '-') {
            if (strcmp(arg, "--help") == 0) {
                print_usage();
                return 0;
            } else if (strcmp(arg, "--version") == 0) {
                puts(PROGRAM " (Winshell) " VERSION);
                return 0;
            } else if (strcmp(arg, "--append") == 0) {
                append = 1;
            } else if (strcmp(arg, "--ignore-interrupts") == 0 || strncmp(arg, "--output-error", 14) == 0) {
                // accepted
            } else {
                fprintf(stderr, PROGRAM ": unrecognized option '%s'\nTry 'tee --help' for more information.\n", arg);
                return 1;
            }
        } else {
            const char* p;
            for (p = arg + 1; *p != '\0'; p++) {
                if (*p == 'a') {
                    append = 1;
                } else if (*p != 'i' && *p != 'p') {
                    fprintf(stderr, PROGRAM ": invalid option -- '%c'\nTry 'tee --help' for more information.\n", *p);
                    return 1;
                }
            }
        }
    }

    text_binary_streams();

    // the files that could be opened; the others are reported now and left out
    {
        size_t opened = 0;
        for (i = 0; i < fileCount; i++) {
            char native[VPATH_MAX];
            FILE* file = NULL;

            if (vpath_to_native(names[i], native, sizeof native) == 0)
                file = fopen(native, append ? "ab" : "wb");
            if (file == NULL) {
                const char* reason = errno == ENOENT ? "No such file or directory" : fs_strerror(GetLastError());
                if (errno == EISDIR || (GetFileAttributesA(native) != INVALID_FILE_ATTRIBUTES &&
                                        (GetFileAttributesA(native) & FILE_ATTRIBUTE_DIRECTORY)))
                    reason = "Is a directory";
                fprintf(stderr, PROGRAM ": %s: %s\n", names[i], reason);
                status = 1;
                continue;
            }
            files[opened++] = file;
        }
        fileCount = opened;
    }

    for (;;) {
        long got = text_read_some(stdin, buffer, sizeof buffer);

        if (got < 0) {
            fprintf(stderr, PROGRAM ": standard input: read error\n");
            status = 1;
            break;
        }
        if (got == 0)
            break;

        if (fwrite(buffer, 1, (size_t)got, stdout) != (size_t)got || fflush(stdout) != 0) {
            fprintf(stderr, PROGRAM ": standard output: write error\n");
            status = 1;
            break;
        }
        for (i = 0; i < fileCount; i++) {
            if (files[i] != NULL && (fwrite(buffer, 1, (size_t)got, files[i]) != (size_t)got || fflush(files[i]) != 0)) {
                fprintf(stderr, PROGRAM ": write error\n");
                fclose(files[i]);
                files[i] = NULL;
                status = 1;
            }
        }
    }

    for (i = 0; i < fileCount; i++)
        if (files[i] != NULL && fclose(files[i]) != 0)
            status = 1;
    return status;
}
