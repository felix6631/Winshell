#include "executer.h"
#include "vpath.h"
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

/*
 * About handles: a child inherits every inheritable handle of the shell, not
 * only the ones given as its standard handles. A pipe end that leaked into a
 * process other than its two users would keep the pipe open and the reader
 * would never see the end of the input. So pipes are created non-inheritable,
 * and each child gets inheritable copies that live only around its
 * CreateProcess call.
 */

static HANDLE duplicate_inheritable(HANDLE handle) {
    HANDLE copy;
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &copy, 0, TRUE, DUPLICATE_SAME_ACCESS))
        return INVALID_HANDLE_VALUE;
    return copy;
}

/*
 * The file names of redirections are paths of the virtual file system
 * (/c/Users/me/file); they are converted here for Windows.
 */
static HANDLE open_input_file(const char* file) {
    SECURITY_ATTRIBUTES sa;
    char native[VPATH_MAX];

    if (vpath_to_native(file, native, sizeof native) != 0)
        return INVALID_HANDLE_VALUE;

    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    return CreateFileA(native, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static HANDLE open_output_file(const char* file, int append) {
    SECURITY_ATTRIBUTES sa;
    char native[VPATH_MAX];

    if (vpath_to_native(file, native, sizeof native) != 0)
        return INVALID_HANDLE_VALUE;

    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    // FILE_APPEND_DATA without FILE_WRITE_DATA makes every write go to the end
    return CreateFileA(native, append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, &sa,
                       append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void close_handle(HANDLE* handle) {
    if (*handle != NULL && *handle != INVALID_HANDLE_VALUE)
        CloseHandle(*handle);
    *handle = NULL;
}

/**
 * Starts one program with the given standard handles, which have to be inheritable.
 * Returns the process handle, or NULL after reporting the problem.
 */
static HANDLE start_program(char* const argv[], HANDLE in, HANDLE out, HANDLE err) {
    char path[MAX_PATH];
    char* command;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    if (resolve_program(argv[0], path, sizeof path) != 0)
        goto err_start;

    command = build_command_line(argv);
    if (command == NULL)
        goto err_start;

    if (strlen(command) >= MAX_COMMAND_LINE) {
        fprintf(stderr, "Error: Command line is too long. Max limit is %d characters\n", MAX_COMMAND_LINE - 1);
        free(command);
        return NULL;
    }

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = out;
    si.hStdError = err;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(
        path,               // executable to run
        command,            // command line (must be writable)
        NULL,               // process security attributes
        NULL,               // thread security attributes
        TRUE,               // child process inherits the handles given above
        0,                  // creation flags; using the same console window
        NULL,               // environment block; use parent's environment
        NULL,               // current directory; use parent's current directory
        &si,                // startup information
        &pi)                // process information
    ) {
        free(command);
        goto err_start;
    }
    free(command);

    CloseHandle(pi.hThread);
    return pi.hProcess;

err_start:
    fprintf(stderr, "Error: Failed to execute command '%s'\n", argv[0]);
    return NULL;
}

int execute_pipeline(const Pipeline* pipeline) {
    HANDLE shellIn  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE shellOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE shellErr = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE* processes;
    HANDLE pipeRead = NULL;     // read end of the pipe coming from the previous command
    DWORD exitCode = (DWORD)-1;
    size_t i;

    if (pipeline->count == 0)
        return 0;

    processes = calloc(pipeline->count, sizeof *processes);
    if (processes == NULL)
        return -1;

    // the shell's own standard handles are handed over as they are
    SetHandleInformation(shellIn,  HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(shellOut, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(shellErr, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    for (i = 0; i < pipeline->count; i++) {
        const Command* command = &pipeline->commands[i];
        int isLast = (i + 1 == pipeline->count);
        HANDLE nextRead = NULL, pipeWrite = NULL;
        HANDLE in = shellIn, out = shellOut, err = shellErr;
        HANDLE ownedIn = NULL, ownedOut = NULL, ownedErr = NULL;   // copies to close afterwards
        int ready = 1;

        if (!isLast && !CreatePipe(&nextRead, &pipeWrite, NULL, 0)) {
            fprintf(stderr, "Error: Failed to create a pipe\n");
            close_handle(&pipeRead);
            break;
        }

        if (command->input != NULL) {
            ownedIn = open_input_file(command->input);
            if (ownedIn == INVALID_HANDLE_VALUE) {
                fprintf(stderr, "Error: Cannot open '%s' for reading\n", command->input);
                ownedIn = NULL;
                ready = 0;
            }
            in = ownedIn;
        } else if (pipeRead != NULL) {
            ownedIn = duplicate_inheritable(pipeRead);
            in = ownedIn;
        }

        if (command->output != NULL) {
            ownedOut = open_output_file(command->output, command->append);
            if (ownedOut == INVALID_HANDLE_VALUE) {
                fprintf(stderr, "Error: Cannot open '%s' for writing\n", command->output);
                ownedOut = NULL;
                ready = 0;
            }
            out = ownedOut;
        } else if (pipeWrite != NULL) {
            ownedOut = duplicate_inheritable(pipeWrite);
            out = ownedOut;
        }

        if (command->errput != NULL) {
            ownedErr = open_output_file(command->errput, command->errAppend);
            if (ownedErr == INVALID_HANDLE_VALUE) {
                fprintf(stderr, "Error: Cannot open '%s' for writing\n", command->errput);
                ownedErr = NULL;
                ready = 0;
            }
            err = ownedErr;
        }

        if (ready)
            processes[i] = start_program(command->argv, in, out, err);

        // The shell must not keep any pipe end of its own, or EOF never arrives.
        close_handle(&ownedIn);
        close_handle(&ownedOut);
        close_handle(&ownedErr);
        close_handle(&pipeRead);
        close_handle(&pipeWrite);
        pipeRead = nextRead;
    }
    close_handle(&pipeRead);

    for (i = 0; i < pipeline->count; i++) {
        if (processes[i] == NULL)
            continue;
        WaitForSingleObject(processes[i], INFINITE);
        if (i + 1 == pipeline->count && !GetExitCodeProcess(processes[i], &exitCode))
            exitCode = (DWORD)-1;
        CloseHandle(processes[i]);
    }

    if (processes[pipeline->count - 1] == NULL)
        exitCode = (DWORD)-1;

    free(processes);
    return (int)exitCode;
}
