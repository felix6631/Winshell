#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"

#define VERSION "0.1"

static void print_usage(void) {
    puts("Usage: pwd [OPTION]...\n"
         "Print the full filename of the current working directory.\n"
         "\n"
         "  -L, --logical    use PWD from the environment (default)\n"
         "  -P, --physical   ignore PWD and print the directory Windows has\n"
         "      --help       display this help and exit\n"
         "      --version    output version information and exit\n"
         "\n"
         "The directory is printed as a path of the virtual file system:\n"
         "C:\\Users\\me is /c/Users/me, and / is the root directory above all drives.");
}

int main(int argc, char* argv[]) {
    char path[VPATH_MAX];
    int physical = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--logical") == 0) {
            physical = 0;
        } else if (strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "--physical") == 0) {
            physical = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            puts("pwd (Winshell) " VERSION);
            return 0;
        } else {
            fprintf(stderr, "pwd: invalid option -- '%s'\nTry 'pwd --help' for more information.\n", argv[i]);
            return 1;
        }
    }

    if (physical) {
        char native[VPATH_MAX];
        DWORD length = GetCurrentDirectoryA(sizeof native, native);
        if (length == 0 || length >= sizeof native) {
            fprintf(stderr, "pwd: error getting current directory\n");
            return 1;
        }
        // a directory outside of the drives (UNC) has no virtual path; show it as Windows does
        if (vpath_from_native(native, path, sizeof path) != 0)
            strcpy(path, native);
    } else if (vpath_getcwd(path, sizeof path) != 0) {
        DWORD length = GetCurrentDirectoryA(sizeof path, path);
        if (length == 0 || length >= sizeof path) {
            fprintf(stderr, "pwd: error getting current directory\n");
            return 1;
        }
    }

    printf("%s\n", path);
    return 0;
}
