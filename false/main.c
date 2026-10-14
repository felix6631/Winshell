/*
 * false: does nothing, unsuccessfully. As in GNU false, the arguments are ignored; a lone --help or --version is
 * answered.
 */

#include <stdio.h>
#include <string.h>

#define PROGRAM "false"
#define STATUS 1

int main(int argc, char* argv[]) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: " PROGRAM " [ignored command line arguments]\n"
             "  or:  " PROGRAM " OPTION\n"
             "Exit with a status code indicating failure.\n"
             "\n"
             "      --help     display this help and exit\n"
             "      --version  output version information and exit\n"
             "\n"
             "Your shell may have its own version of " PROGRAM ", which usually supersedes\n"
             "the version described here.");
    } else if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts(PROGRAM " 0.1");
    }
    return STATUS;
}
