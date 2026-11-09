#ifndef EXEC_H
#define EXEC_H

#include <windows.h>

/*
 * Runs a command for the tools that wrap one (nice, nohup): the program is found as env finds it, the child gets the
 * given standard handles, and the result is the exit status the wrapper has to return. A command that is not found
 * gives 127 and one that cannot be started 126, with the message of the tool: nohup says "failed to run command
 * 'x': ...", the others "'x': ...".
 */
int exec_run(const char* tool, int longMessage, char* const argv[], DWORD creationFlags, HANDLE in, HANDLE out, HANDLE err);

#endif //EXEC_H
