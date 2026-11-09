/*
 * exec: starts a command for the wrappers nice and nohup and waits for it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "cmdline.h"
#include "exec.h"
#include "fsutil.h"
#include "program.h"

int exec_run(const char* tool, int longMessage, char* const argv[], DWORD creationFlags, HANDLE in, HANDLE out, HANDLE err) {
    char path[MAX_PATH * 2];
    char* command;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code;

    if (program_find(argv[0], path, sizeof path) != 0) {
        if (longMessage)
            fprintf(stderr, "%s: failed to run command '%s': No such file or directory\n", tool, argv[0]);
        else
            fprintf(stderr, "%s: '%s': No such file or directory\n", tool, argv[0]);
        return 127;
    }
    command = cmdline_build(argv);
    if (command == NULL) {
        fprintf(stderr, "%s: memory exhausted\n", tool);
        return 125;
    }
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = out;
    si.hStdError = err;
    SetHandleInformation(in, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(out, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(err, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    fflush(stdout);
    if (!CreateProcessA(path, command, NULL, NULL, TRUE, creationFlags, NULL, NULL, &si, &pi)) {
        DWORD error = GetLastError();

        if (longMessage)
            fprintf(stderr, "%s: failed to run command '%s': %s\n", tool, argv[0], fs_strerror(error));
        else
            fprintf(stderr, "%s: '%s': %s\n", tool, argv[0], fs_strerror(error));
        free(command);
        return 126;
    }
    free(command);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (!GetExitCodeProcess(pi.hProcess, &code))
        code = 125;
    CloseHandle(pi.hProcess);
    return (int)code;
}
