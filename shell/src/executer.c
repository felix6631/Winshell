#include "executer.h"
#include "vpath.h"
#include "program.h"
#include "cmdline.h"
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_COMMAND_LINE 32767

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
 * The environment of a command that has NAME=value words in front of it: the
 * one of the shell, with those variables set. Returns NULL when there are
 * none (the program then inherits the environment), or when out of memory
 * (*failed is set). The result has to be freed.
 */
static char* build_environment(const Command* command, int* failed) {
    char* current;
    char* block;
    size_t size = 1, used = 0, i;
    const char* p;

    if (command->assignmentCount == 0)
        return NULL;

    current = GetEnvironmentStringsA();
    if (current == NULL) {
        *failed = 1;
        return NULL;
    }
    for (p = current; *p != '\0'; p += strlen(p) + 1)
        size += strlen(p) + 1;
    for (i = 0; i < command->assignmentCount; i++)
        size += strlen(command->assignments[i]) + 1;

    block = malloc(size);
    if (block == NULL) {
        FreeEnvironmentStringsA(current);
        *failed = 1;
        return NULL;
    }

    // the variables of the shell that the assignments do not replace (names are not case sensitive)
    for (p = current; *p != '\0'; p += strlen(p) + 1) {
        const char* equals = strchr(p + (*p == '=' ? 1 : 0), '=');
        size_t nameLength = equals != NULL ? (size_t)(equals - p) : strlen(p);
        int replaced = 0;

        for (i = 0; i < command->assignmentCount && !replaced; i++) {
            const char* assignment = command->assignments[i];
            const char* assignmentEquals = strchr(assignment, '=');
            replaced = assignmentEquals != NULL && (size_t)(assignmentEquals - assignment) == nameLength &&
                       _strnicmp(assignment, p, nameLength) == 0;
        }
        if (!replaced) {
            size_t length = strlen(p) + 1;
            memcpy(block + used, p, length);
            used += length;
        }
    }
    FreeEnvironmentStringsA(current);

    for (i = 0; i < command->assignmentCount; i++) {
        const char* assignment = command->assignments[i];
        size_t nameLength = strcspn(assignment, "=");
        size_t length = strlen(assignment) + 1;
        size_t j;
        int overridden = 0;

        for (j = i + 1; j < command->assignmentCount && !overridden; j++)
            overridden = strcspn(command->assignments[j], "=") == nameLength &&
                         _strnicmp(command->assignments[j], assignment, nameLength) == 0;
        if (overridden)
            continue;
        memcpy(block + used, assignment, length);
        used += length;
    }
    block[used] = '\0';
    return block;
}

/**
 * Starts one program with the given standard handles, which have to be inheritable.
 * Returns the process handle, or NULL after reporting the problem.
 */
static HANDLE start_program(char* const argv[], HANDLE in, HANDLE out, HANDLE err, char* environment) {
    char path[MAX_PATH];
    char* command;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    if (program_find(argv[0], path, sizeof path) != 0)
        goto err_start;

    command = cmdline_build(argv);
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
        environment,        // environment block; NULL is the parent's environment
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

#define COMMAND_READY 1     /* the files are open, the environment is built */
#define COMMAND_FAILED 0    /* a message was printed; the neighbours see an empty input or a closed output */
#define COMMAND_NOMEM (-1)

/**
 * Starts one command of a pipeline. pipeRead is the end of the pipe coming from the
 * previous command and pipeWrite the one going to the next, or NULL; the shell keeps
 * both (the caller closes them), and gives the program duplicates of them.
 * *process gets the program, or NULL when none was started (a failure, or NAME=value alone).
 */
static int start_command(const Command* command, HANDLE pipeRead, HANDLE pipeWrite,
                         HANDLE shellIn, HANDLE shellOut, HANDLE shellErr, HANDLE* process) {
    HANDLE in = shellIn, out = shellOut, err = shellErr;
    // the copies and files opened for this command, to close afterwards; descriptors that
    // were made copies of another one (2>&1) refer to the same handle, which is closed once
    HANDLE* owned = malloc((2 + command->redirectCount) * sizeof *owned);
    size_t ownedCount = 0, r;
    char* environment;
    int ready = 1, failed = 0;

    *process = NULL;
    if (owned == NULL) {
        fprintf(stderr, "Error: Out of memory\n");
        return COMMAND_NOMEM;
    }

    if (command->input != NULL) {
        in = open_input_file(command->input);
        if (in == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Error: Cannot open '%s' for reading\n", command->input);
            in = NULL;
            ready = 0;
        } else {
            owned[ownedCount++] = in;
        }
    } else if (pipeRead != NULL) {
        in = duplicate_inheritable(pipeRead);
        owned[ownedCount++] = in;
    }

    if (pipeWrite != NULL) {
        out = duplicate_inheritable(pipeWrite);
        owned[ownedCount++] = out;
    }

    // standard output and error, as written: a later redirection does not change a copy made before it
    for (r = 0; r < command->redirectCount; r++) {
        const Redirect* redirect = &command->redirects[r];
        HANDLE* slot = redirect->fd == 2 ? &err : &out;

        if (redirect->file == NULL) {
            *slot = redirect->dupOf == 2 ? err : out;
        } else {
            HANDLE file = open_output_file(redirect->file, redirect->append);
            if (file == INVALID_HANDLE_VALUE) {
                fprintf(stderr, "Error: Cannot open '%s' for writing\n", redirect->file);
                ready = 0;
            } else {
                owned[ownedCount++] = file;
                *slot = file;
            }
        }
    }

    environment = build_environment(command, &failed);
    if (failed) {
        fprintf(stderr, "Error: Out of memory\n");
        ready = 0;
    }

    // NAME=value alone starts nothing; its redirections were still made (and truncate their files)
    if (ready && command->argv != NULL)
        *process = start_program(command->argv, in, out, err, environment);
    free(environment);

    // The shell must not keep any pipe end of its own, or EOF never arrives.
    for (r = 0; r < ownedCount; r++)
        CloseHandle(owned[r]);
    free(owned);
    return ready ? COMMAND_READY : COMMAND_FAILED;
}

int execute_pipeline(const Pipeline* pipeline) {
    HANDLE shellIn  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE shellOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE shellErr = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE* processes;
    HANDLE pipeRead = NULL;     // read end of the pipe coming from the previous command
    DWORD exitCode = (DWORD)-1;
    int noopSucceeded = 0;      // the last command was NAME=value alone
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
        int result;

        if (!isLast && !CreatePipe(&nextRead, &pipeWrite, NULL, 0)) {
            fprintf(stderr, "Error: Failed to create a pipe\n");
            break;
        }

        result = start_command(command, pipeRead, pipeWrite, shellIn, shellOut, shellErr, &processes[i]);
        if (result == COMMAND_READY && command->argv == NULL && isLast)
            noopSucceeded = 1;

        close_handle(&pipeRead);
        close_handle(&pipeWrite);
        pipeRead = nextRead;
        if (result == COMMAND_NOMEM) {
            close_handle(&pipeRead);
            break;
        }
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

    if (noopSucceeded)
        exitCode = 0;
    else if (processes[pipeline->count - 1] == NULL)
        exitCode = (DWORD)-1;

    free(processes);
    return (int)exitCode;
}

/* ---- redirections of the shell itself, for compound commands ------------------------------- */

void redirect_end(StdRedirection* r) {
    int fd;

    fflush(stdout);
    fflush(stderr);
    for (fd = 0; fd < 3; fd++) {
        if (r->saved[fd] >= 0) {
            _dup2(r->saved[fd], fd);
            _close(r->saved[fd]);
            r->saved[fd] = -1;
        }
    }
}

static int redirect_save(StdRedirection* r, int fd) {
    if (r->saved[fd] < 0)
        r->saved[fd] = _dup(fd);
    return r->saved[fd] >= 0;
}

int redirect_begin(const Command* command, StdRedirection* r) {
    size_t i;

    r->saved[0] = r->saved[1] = r->saved[2] = -1;
    r->inputRedirected = 0;
    fflush(stdout);
    fflush(stderr);

    if (command->input != NULL) {
        char native[VPATH_MAX];
        int fd;

        if (vpath_to_native(command->input, native, sizeof native) != 0 ||
            (fd = _open(native, _O_RDONLY | _O_BINARY)) < 0) {
            fprintf(stderr, "Error: Cannot open '%s' for reading\n", command->input);
            redirect_end(r);
            return -1;
        }
        if (!redirect_save(r, 0)) {
            _close(fd);
            redirect_end(r);
            return -1;
        }
        _dup2(fd, 0);
        _close(fd);
        r->inputRedirected = 1;
    }

    for (i = 0; i < command->redirectCount; i++) {
        const Redirect* redirect = &command->redirects[i];
        int slot = redirect->fd == 2 ? 2 : 1;

        if (!redirect_save(r, slot)) {
            redirect_end(r);
            return -1;
        }
        if (redirect->file == NULL) {
            _dup2(redirect->dupOf == 2 ? 2 : 1, slot);
        } else {
            char native[VPATH_MAX];
            int fd = -1;

            if (vpath_to_native(redirect->file, native, sizeof native) == 0) {
                // through the handle: FILE_APPEND_DATA makes every write, also of the programs that
                // share it, go to the end
                HANDLE h = open_output_file(redirect->file, redirect->append);

                if (h != INVALID_HANDLE_VALUE) {
                    fd = _open_osfhandle((intptr_t)h, _O_WRONLY | _O_BINARY | (redirect->append ? _O_APPEND : 0));
                    if (fd < 0)
                        CloseHandle(h);
                }
            }
            if (fd < 0) {
                fprintf(stderr, "Error: Cannot open '%s' for writing\n", redirect->file);
                redirect_end(r);
                return -1;
            }
            _dup2(fd, slot);
            _close(fd);
        }
    }
    return 0;
}
