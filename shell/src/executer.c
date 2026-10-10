#include "executer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_COMMAND_LINE 32767

/**
 * Writes arg into out, quoted following the rules of CommandLineToArgvW so
 * the child gets the very same argument back. Returns the end of the output.
 */
static char* quote_arg(char* out, const char* arg) {
    size_t backslashes, i;

    if (*arg != '\0' && strpbrk(arg, " \t\"") == NULL) {
        size_t length = strlen(arg);
        memcpy(out, arg, length);
        return out + length;
    }

    *out++ = '"';
    for (;;) {
        backslashes = 0;
        while (*arg == '\\') {
            arg++;
            backslashes++;
        }

        if (*arg == '\0') {
            // backslashes in front of the closing quote must be doubled
            for (i = 0; i < backslashes * 2; i++)
                *out++ = '\\';
            break;
        }

        if (*arg == '"') {
            // backslashes in front of a quote must be doubled, and the quote escaped
            for (i = 0; i < backslashes * 2 + 1; i++)
                *out++ = '\\';
        } else {
            for (i = 0; i < backslashes; i++)
                *out++ = '\\';
        }
        *out++ = *arg++;
    }
    *out++ = '"';
    return out;
}

/** Joins argv into one command line. The result has to be freed. */
static char* build_command_line(char* const argv[]) {
    size_t size = 1, i;
    char *command, *end;

    for (i = 0; argv[i] != NULL; i++)
        size += strlen(argv[i]) * 2 + 3;

    command = malloc(size);
    if (command == NULL)
        return NULL;

    end = command;
    for (i = 0; argv[i] != NULL; i++) {
        if (i > 0)
            *end++ = ' ';
        end = quote_arg(end, argv[i]);
    }
    *end = '\0';
    return command;
}

/** Builds "<directory of shell.exe>\<program>.exe" into path. */
static int resolve_program(const char* program, char* path, size_t size) {
    char directory[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, directory, MAX_PATH);
    char* separator;
    size_t program_length = strlen(program);
    int written;

    if (length == 0 || length >= MAX_PATH)
        return -1;

    separator = strrchr(directory, '\\');
    if (separator == NULL)
        return -1;
    *separator = '\0';

    if (program_length >= 4 && _stricmp(program + program_length - 4, ".exe") == 0)
        written = snprintf(path, size, "%s\\%s", directory, program);
    else
        written = snprintf(path, size, "%s\\%s.exe", directory, program);

    return (written < 0 || (size_t)written >= size) ? -1 : 0;
}

int execute(const char* program, char* const argv[]) {
    char path[MAX_PATH];
    char* command;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD exitCode;

    if (resolve_program(program, path, sizeof path) != 0)
        return -1;

    command = build_command_line(argv);
    if (command == NULL)
        return -1;

    if (strlen(command) >= MAX_COMMAND_LINE) {
        fprintf(stderr, "Error: Command line is too long. Max limit is %d characters\n", MAX_COMMAND_LINE - 1);
        free(command);
        return -1;
    }

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(
        path,               // executable to run
        command,            // command line (must be writable)
        NULL,               // process security attributes
        NULL,               // thread security attributes
        TRUE,               // child process inherits handles to use same input/output streams
        0,                  // creation flags; using the same console window
        NULL,               // environment block; use parent's environment
        NULL,               // current directory; use parent's current directory
        &si,                // startup information
        &pi)                // process information
    ) {
        free(command);
        return -1;
    }
    free(command);

    WaitForSingleObject(pi.hProcess, INFINITE);
    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
        exitCode = (DWORD)-1;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exitCode;
}
