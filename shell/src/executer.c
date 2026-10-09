#include "executer.h"
#include <windows.h>
#include <stdlib.h>
int execute(const char* program, ...) {
    char* command = (char*)malloc(sizeof(char) * 1024);
    va_list args;
    va_start(args, program);
    vsnprintf(command, 1024, program, args);

    va_end(args);
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));


    if (!CreateProcess(
        NULL,               // execute path
        command,            // command and arguments
        NULL,               // process security attributes
        NULL,               // thread security attributes
        TRUE,               // child process inherits handles to use same input/output streams
        0,                  // creation flags; not creating new window, rather using the same console window
        NULL,               // environment block; use parent's environment
        "/bin",             // current directory; use parent's current directory
        &si,                // startup information
        &pi)                // process information
    ) {
        free(command);
        return -1;
    }

    free(command);
    return 0;
}