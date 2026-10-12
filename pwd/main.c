#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"
#include "args.h"

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

enum { OPT_LOGICAL = 1, OPT_PHYSICAL };

static const ArgsOption optionList[] = {
    { OPT_LOGICAL,  'L', "logical",  ARGS_NONE },
    { OPT_PHYSICAL, 'P', "physical", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { "pwd", VERSION, print_usage, optionList, 1, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    (void)value;
    *(int*)context = id == OPT_PHYSICAL;
    return 0;
}

int main(int argc, char* argv[]) {
    char path[VPATH_MAX];
    const char** operands;
    int physical = 0, count, result;

    result = args_parse(&spec, argc, argv, on_option, &physical, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

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
