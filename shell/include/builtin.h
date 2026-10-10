#ifndef BUILTIN_H
#define BUILTIN_H

#include "parser.h"

/**
 * Commands that have to run inside the shell process itself, because they
 * change the state of the shell.
 *
 *   cd [DIR]   change the current directory; "cd" goes to the home directory
 *              (USERPROFILE), "cd -" to the previous directory.
 *              DIR is a path of the virtual file system, "/" being the root.
 */

/** Makes the environment variable PWD describe the directory the shell starts in. */
void builtin_init(void);

/**
 * Runs the command when it is a builtin.
 * Returns non-zero when it was one, and zero when it has to be started as a program.
 */
int builtin_run(const Command* command);

#endif //BUILTIN_H
