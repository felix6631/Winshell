#ifndef PROGRAM_H
#define PROGRAM_H

#include <stddef.h>

/**
 * Finds the program a command word names, for the shell and for env.
 *
 * A name without a path ("sort", "sort.exe") is a program of the directory of
 * the running executable, which is the bin directory of the project. A word
 * with a path separator is a path of the virtual file system (or a Windows
 * path); ".exe" is added when the file is not found as it is.
 *
 * Returns 0 and the native path of an existing file, or -1.
 */
int program_find(const char* program, char* path, size_t size);

#endif //PROGRAM_H
