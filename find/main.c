#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "find.h"

#define VERSION "0.1"
#define PROGRAM "find"

static void print_usage(void) {
    puts("Usage: find [-H] [-L] [-P] [PATH...] [EXPRESSION]\n"
         "Search for files in a directory hierarchy (PATH defaults to .).\n"
         "\n"
         "Options (symbolic links are not followed unless -L): -H -L -P\n"
         "\n"
         "Operators, highest precedence first:\n"
         "  ( EXPR )   ! EXPR   -not EXPR   EXPR -a EXPR   EXPR -o EXPR   EXPR , EXPR\n"
         "\n"
         "Global options: -maxdepth N  -mindepth N  -depth  -follow  -xdev  -mount\n"
         "\n"
         "Tests (N is +N more than, -N less than, N exactly):\n"
         "  -name PATTERN  -iname PATTERN  -path PATTERN  -ipath PATTERN\n"
         "  -regex REGEX   -iregex REGEX   -regextype emacs|posix-basic|posix-extended\n"
         "  -type [fdl,]   -xtype [fdl,]   -size [+-]N[cwbkMG]   -empty\n"
         "  -newer FILE    -mtime N  -atime N  -ctime N  -mmin N  -amin N  -cmin N\n"
         "  -readable  -writable  -executable  -true  -false\n"
         "\n"
         "Actions (-print is used when there is none):\n"
         "  -print  -print0  -printf FORMAT  -delete  -prune  -quit\n"
         "  -exec COMMAND ;   -exec COMMAND {} +   -execdir COMMAND ;   -ok COMMAND ;\n"
         "\n"
         "Paths are paths of the virtual file system. Not supported: -perm, -user, -group,\n"
         "-links, -inum, -ls, -fprint and the other -f* actions.");
}

int main(int argc, char* argv[]) {
    FindOptions options;
    const char* starts[256];
    int startCount = 0, i = 1, ok;
    Node* expression;

    memset(&options, 0, sizeof options);
    options.maxDepth = -1;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-version") == 0)) {
        printf(PROGRAM " (Winshell) " VERSION "\n");
        return 0;
    }

    // -H -L -P come first
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-L") == 0)
            options.follow = 1;
        else if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "-P") == 0)
            options.follow = 0;
        else
            break;
    }
    // the paths: up to the first argument that begins the expression
    for (; i < argc; i++) {
        const char* a = argv[i];
        if (a[0] == '-' || strcmp(a, "(") == 0 || strcmp(a, ")") == 0 || strcmp(a, "!") == 0 || strcmp(a, ",") == 0)
            break;
        if (startCount < 256)
            starts[startCount++] = a;
    }
    if (startCount == 0)
        starts[startCount++] = ".";

    expression = find_parse(argc - i, argv + i, &options, &ok);
    if (!ok)
        return 1;

    if (!options.hasAction) {
        Node* print = calloc(1, sizeof *print);
        if (print == NULL)
            return 1;
        print->kind = N_TEST;
        print->pred = P_PRINT;
        if (expression == NULL) {
            expression = print;
        } else {
            Node* both = calloc(1, sizeof *both);
            if (both == NULL)
                return 1;
            both->kind = N_AND;
            both->left = expression;
            both->right = print;
            expression = both;
        }
    }
    return find_walk(expression, &options, starts, startCount);
}
