#ifndef EXECUTER_H
#define EXECUTER_H

/**
 * execute runs the program located in the same directory as the shell
 * (the project's bin directory), passes argv to it, and waits for it to exit.
 *
 * program is the command name; ".exe" is appended when missing.
 * argv is a NULL-terminated array, argv[0] being the program name by
 * convention. The number of arguments is only bounded by the Windows
 * command line limit (32767 characters).
 *
 * It returns the exit code of the program, or -1 when the program could not
 * be started.
 */
int execute(const char* program, char* const argv[]);

#endif //EXECUTER_H
