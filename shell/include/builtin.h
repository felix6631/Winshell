#ifndef BUILTIN_H
#define BUILTIN_H

#include "parser.h"

/**
 * Commands that have to run inside the shell process itself, because they
 * change the state of the shell.
 *
 *   cd [DIR]          change the current directory; "cd" goes to the home directory
 *                     (USERPROFILE), "cd -" to the previous directory.
 *                     DIR is a path of the virtual file system, "/" being the root.
 *   export NAME=VALUE set variables, which the programs started later have as well;
 *                     alone it lists them
 *   unset NAME        remove variables
 *   exit [N], quit    leave the shell, with status N (default: that of the last command)
 */

/** Makes the environment variable PWD describe the directory the shell starts in. */
void builtin_init(void);

/**
 * Runs the command when it is a builtin.
 * Returns non-zero when it was one, and zero when it has to be started as a program.
 * *status is set to its exit status; *exitShell to non-zero when the shell is to end.
 * *status keeps its value for "exit" without an argument.
 */
int builtin_run(const Command* command, int* status, int* exitShell);

#endif //BUILTIN_H
