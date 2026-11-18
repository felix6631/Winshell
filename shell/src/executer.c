#include "executer.h"
#include "command.h"
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
/** The name of a program without its directory and its extension. */
static void base_name(const char* path, char* out, size_t size) {
    const char* slash = strrchr(path, '/');
    const char* back = strrchr(path, '\\');
    const char* name = path;
    size_t n;

    if (slash != NULL && slash + 1 > name)
        name = slash + 1;
    if (back != NULL && back + 1 > name)
        name = back + 1;
    n = strlen(name);
    if (n > 4 && _stricmp(name + n - 4, ".exe") == 0)
        n -= 4;
    if (n >= size)
        n = size - 1;
    memcpy(out, name, n);
    out[n] = '\0';
}

/**
 * A file that is not a program of Windows is a script: `#!/bin/sh`, `#!/usr/bin/env python3`, or text with no first line
 * (a shell script). Returns 1 with the interpreter (a Windows path) and the one argument the first line may give it,
 * 0 for a program that Windows runs itself, -1 when the interpreter cannot be found.
 */
static int script_interpreter(const char* path, char* interpreter, size_t size, char* argument, size_t argumentSize) {
    unsigned char head[256];
    DWORD got = 0;
    HANDLE file;
    size_t length = strlen(path), i;
    char first[128], second[128], third[128];
    int words = 0;

    argument[0] = '\0';
    if (length > 4 && (_stricmp(path + length - 4, ".exe") == 0 || _stricmp(path + length - 4, ".com") == 0 ||
                       _stricmp(path + length - 4, ".cmd") == 0 || _stricmp(path + length - 4, ".bat") == 0))
        return 0;
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    if (!ReadFile(file, head, sizeof head - 1, &got, NULL))
        got = 0;
    CloseHandle(file);
    if (got >= 2 && head[0] == 'M' && head[1] == 'Z')
        return 0;
    head[got] = '\0';

    first[0] = second[0] = third[0] = '\0';
    if (got >= 2 && head[0] == '#' && head[1] == '!') {
        char line[256];
        char* word;
        char* context = NULL;

        for (i = 2; i < got && head[i] != '\n' && head[i] != '\r'; i++)
            line[i - 2] = (char)head[i];
        line[i > 2 ? i - 2 : 0] = '\0';
        for (word = strtok_s(line, " \t", &context); word != NULL && words < 3; word = strtok_s(NULL, " \t", &context)) {
            snprintf(words == 0 ? first : (words == 1 ? second : third), 128, "%s", word);
            words++;
        }
    }
    {
        char name[128];
        const char* wanted;
        char native[MAX_PATH];

        if (words == 0) {
            snprintf(name, sizeof name, "sh");              // text without a first line: a shell script
        } else {
            base_name(first, name, sizeof name);
            if (strcmp(name, "env") == 0 && words >= 2) {
                size_t k = 1;
                const char* args[2] = { second, third };

                while (k <= 2 && args[k - 1][0] == '-' && k < (size_t)words)
                    k++;                                    // env -S and the like
                base_name(args[k - 1], name, sizeof name);
            } else if (words >= 2) {
                snprintf(argument, argumentSize, "%s", second);
            }
        }
        wanted = name;
        if (strcmp(name, "sh") == 0 || strcmp(name, "bash") == 0 || strcmp(name, "dash") == 0 || strcmp(name, "zsh") == 0 ||
            strcmp(name, "ash") == 0 || strcmp(name, "ksh") == 0 || strcmp(name, "shell") == 0) {
            DWORD n = GetModuleFileNameA(NULL, interpreter, (DWORD)size);

            argument[0] = '\0';
            return n > 0 && n < size ? 1 : -1;
        }
        if (strcmp(name, "python3") == 0 || strcmp(name, "python") == 0) {
            // python3 of Windows is often a stub of the Store (WindowsApps) that runs nothing: python comes first
            if (program_find("python", native, sizeof native) != 0 && program_find("python3", native, sizeof native) != 0)
                return -1;
        } else if (program_find(wanted, native, sizeof native) != 0) {
            return -1;
        }
        snprintf(interpreter, size, "%s", native);
        return 1;
    }
}

static HANDLE start_program(char* const argv[], HANDLE in, HANDLE out, HANDLE err, char* environment, DWORD flags) {
    char path[MAX_PATH];
    char interpreter[MAX_PATH], scriptArgument[128], scriptPath[MAX_PATH];
    char** scriptArgv = NULL;
    char* command;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    if (program_find(argv[0], path, sizeof path) != 0)
        goto err_start;

    // a script is run by its interpreter: argv becomes interpreter [argument] script arguments...
    {
        int kind = script_interpreter(path, interpreter, sizeof interpreter, scriptArgument, sizeof scriptArgument);

        if (kind < 0)
            goto err_start;
        if (kind > 0) {
            size_t count = 0, n = 0, k;

            while (argv[count] != NULL)
                count++;
            scriptArgv = calloc(count + 4, sizeof *scriptArgv);
            if (scriptArgv == NULL)
                goto err_start;
            scriptArgv[n++] = interpreter;
            if (scriptArgument[0] != '\0')
                scriptArgv[n++] = scriptArgument;
            snprintf(scriptPath, sizeof scriptPath, "%s", path);
            scriptArgv[n++] = scriptPath;
            for (k = 1; k < count; k++)
                scriptArgv[n++] = argv[k];
            argv = scriptArgv;
            snprintf(path, sizeof path, "%s", interpreter);
        }
    }

    {
        // a batch file is run by cmd.exe, which reads the name of the file from the command line: it has to be a Windows path
        size_t length = strlen(path);
        int isBatch = length > 4 && (_stricmp(path + length - 4, ".cmd") == 0 || _stricmp(path + length - 4, ".bat") == 0);

        if (isBatch) {
            size_t count = 0, i;
            char** copy;

            while (argv[count] != NULL)
                count++;
            copy = calloc(count + 1, sizeof *copy);
            if (copy == NULL)
                goto err_start;
            for (i = 0; i < count; i++)
                copy[i] = argv[i];
            copy[0] = path;
            command = cmdline_build(copy);
            free(copy);
        } else {
            command = cmdline_build(argv);
        }
    }
    if (command == NULL)
        goto err_start;

    if (strlen(command) >= MAX_COMMAND_LINE) {
        fprintf(stderr, "Error: Command line is too long. Max limit is %d characters\n", MAX_COMMAND_LINE - 1);
        free(command);
        free(scriptArgv);
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
        flags,              // creation flags; using the same console window
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
    free(scriptArgv);
    return pi.hProcess;

err_start:
    fprintf(stderr, "Error: Failed to execute command '%s'\n", argv[0]);
    free(scriptArgv);
    return NULL;
}

#define COMMAND_READY 1     /* the files are open, the environment is built */
#define COMMAND_FAILED 0    /* a message was printed; the neighbours see an empty input or a closed output */
#define COMMAND_NOMEM (-1)

/** The descriptors 3 to 9 that the shell holds open (exec 3> file); 0, 1 and 2 are always there. */
static int fdOpen[10];

static int fd_is_open(int fd) {
    return fd >= 0 && fd < 10 && (fd < 3 || fdOpen[fd]);
}

/**
 * Starts one command of a pipeline. pipeRead is the end of the pipe coming from the
 * previous command and pipeWrite the one going to the next, or NULL; the shell keeps
 * both (the caller closes them), and gives the program duplicates of them.
 * *process gets the program, or NULL when none was started (a failure, or NAME=value alone).
 */
static int start_command(const Command* command, HANDLE pipeRead, HANDLE pipeWrite,
                         HANDLE shellIn, HANDLE shellOut, HANDLE shellErr, DWORD flags, HANDLE* process) {
    HANDLE in = shellIn, out = shellOut, err = shellErr;
    HANDLE table[10];
    int k;
    // the copies and files opened for this command, to close afterwards; descriptors that
    // were made copies of another one (2>&1) refer to the same handle, which is closed once
    HANDLE* owned = malloc((3 + command->redirectCount) * sizeof *owned);
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

    // the descriptors 0 to 9 as the program sees them: 3 and up are the ones the shell holds (exec 3> file)
    table[0] = in;
    table[1] = out;
    table[2] = err;
    for (k = 3; k < 10; k++)
        table[k] = fd_is_open(k) ? (HANDLE)_get_osfhandle(k) : NULL;

    // the redirections, as written: a later redirection does not change a copy made before it
    for (r = 0; r < command->redirectCount; r++) {
        const Redirect* redirect = &command->redirects[r];
        int fd = redirect->fd;

        if (fd < 0 || fd > 9)
            continue;
        if (redirect->file == NULL) {
            if (redirect->dupOf < 0) {
                // N>&-: a program cannot be started with a closed standard descriptor, so it gets NUL
                HANDLE closed = fd == 0 ? open_input_file("/dev/null") : open_output_file("/dev/null", 0);

                if (closed != INVALID_HANDLE_VALUE) {
                    owned[ownedCount++] = closed;
                    table[fd] = closed;
                }
            } else if (redirect->dupOf > 9 || table[redirect->dupOf] == NULL) {
                fprintf(stderr, "Error: %d: Bad file descriptor\n", redirect->dupOf);
                ready = 0;
            } else {
                table[fd] = table[redirect->dupOf];
            }
        } else if (redirect->input) {
            HANDLE file = open_input_file(redirect->file);

            if (file == INVALID_HANDLE_VALUE) {
                fprintf(stderr, "Error: Cannot open '%s' for reading\n", redirect->file);
                ready = 0;
            } else {
                owned[ownedCount++] = file;
                table[fd] = file;
            }
        } else {
            HANDLE file = open_output_file(redirect->file, redirect->append);
            if (file == INVALID_HANDLE_VALUE) {
                fprintf(stderr, "Error: Cannot open '%s' for writing\n", redirect->file);
                ready = 0;
            } else {
                owned[ownedCount++] = file;
                table[fd] = file;
            }
        }
    }
    in = table[0];
    out = table[1];
    err = table[2];
    for (k = 0; k < 3; k++)
        if (table[k] != NULL)
            SetHandleInformation(table[k], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    environment = build_environment(command, &failed);
    if (failed) {
        fprintf(stderr, "Error: Out of memory\n");
        ready = 0;
    }

    // NAME=value alone starts nothing; its redirections were still made (and truncate their files)
    if (ready && command->argv != NULL)
        *process = start_program(command->argv, in, out, err, environment, flags);
    free(environment);

    // The shell must not keep any pipe end of its own, or EOF never arrives.
    for (r = 0; r < ownedCount; r++)
        CloseHandle(owned[r]);
    free(owned);
    return ready ? COMMAND_READY : COMMAND_FAILED;
}

/**
 * Starts the pipeline. In the background nothing is waited for: the processes are handed to the caller (*keep), the
 * input is NUL and the processes are in a group of their own, which Ctrl+C does not reach.
 */
static int run_pipeline(const Pipeline* pipeline, int background, HANDLE** keep, size_t* keepCount) {
    HANDLE shellIn  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE shellOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE shellErr = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE* processes;
    HANDLE pipeRead = NULL;     // read end of the pipe coming from the previous command
    DWORD exitCode = (DWORD)-1;
    int noopSucceeded = 0;      // the last command was NAME=value alone
    int redirectFailed = 0;     // a redirection of the last command could not be made: the status is 1, as in bash
    DWORD failed = 0;           // the status of the last command before the end of the pipeline that failed (set -o pipefail)
    size_t i;

    HANDLE nul = INVALID_HANDLE_VALUE;
    DWORD flags = background ? CREATE_NEW_PROCESS_GROUP : 0;

    if (pipeline->count == 0)
        return 0;

    processes = calloc(pipeline->count, sizeof *processes);
    if (processes == NULL)
        return -1;

    // the shell's own standard handles are handed over as they are
    SetHandleInformation(shellIn,  HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(shellOut, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(shellErr, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    if (background) {
        SECURITY_ATTRIBUTES inheritable = { sizeof inheritable, NULL, TRUE };

        nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, NULL);
        if (nul != INVALID_HANDLE_VALUE)
            shellIn = nul;
    }

    for (i = 0; i < pipeline->count; i++) {
        const Command* command = &pipeline->commands[i];
        int isLast = (i + 1 == pipeline->count);
        HANDLE nextRead = NULL, pipeWrite = NULL;
        int result;

        if (!isLast && !CreatePipe(&nextRead, &pipeWrite, NULL, 0)) {
            fprintf(stderr, "Error: Failed to create a pipe\n");
            break;
        }

        result = start_command(command, pipeRead, pipeWrite, shellIn, shellOut, shellErr, flags, &processes[i]);
        if (result == COMMAND_READY && command->argv == NULL && isLast)
            noopSucceeded = 1;
        if (result == COMMAND_FAILED && isLast)
            redirectFailed = 1;

        close_handle(&pipeRead);
        close_handle(&pipeWrite);
        pipeRead = nextRead;
        if (result == COMMAND_NOMEM) {
            close_handle(&pipeRead);
            break;
        }
    }
    close_handle(&pipeRead);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);

    if (background) {
        size_t started = 0;

        for (i = 0; i < pipeline->count; i++)
            if (processes[i] != NULL)
                processes[started++] = processes[i];
        *keep = processes;
        *keepCount = started;
        return started > 0 ? 0 : -1;
    }

    for (i = 0; i < pipeline->count; i++) {
        if (processes[i] == NULL)
            continue;
        WaitForSingleObject(processes[i], INFINITE);
        if (i + 1 == pipeline->count && !GetExitCodeProcess(processes[i], &exitCode))
            exitCode = (DWORD)-1;
        if (i + 1 != pipeline->count && winshell_opt_pipefail) {
            DWORD code;

            if (GetExitCodeProcess(processes[i], &code) && code != 0)
                failed = code;
        }
        CloseHandle(processes[i]);
    }
    if (winshell_opt_pipefail && failed != 0 && (exitCode == 0 || exitCode == (DWORD)-1))
        exitCode = failed;

    if (noopSucceeded)
        exitCode = 0;
    else if (processes[pipeline->count - 1] == NULL)
        exitCode = redirectFailed ? 1 : (DWORD)-1;

    free(processes);
    return (int)exitCode;
}

int execute_pipeline(const Pipeline* pipeline) {
    return run_pipeline(pipeline, 0, NULL, NULL);
}

int execute_pipeline_background(const Pipeline* pipeline, HANDLE** processes, size_t* count) {
    return run_pipeline(pipeline, 1, processes, count);
}

/* ---- redirections of the shell itself, for compound commands ------------------------------- */

/* the copies of the descriptors that a redirection replaced: numbered from 10, so that 0 to 9 stay free for the user */
static unsigned char copyUsed[256];

static int copy_descriptor(int fd) {
    int n;

    for (n = 10; n < 256; n++) {
        if (!copyUsed[n]) {
            if (_dup2(fd, n) != 0)
                return -1;
            copyUsed[n] = 1;
            return n;
        }
    }
    return -1;
}

static void drop_copy(int n) {
    _close(n);
    copyUsed[n] = 0;
}

void redirect_end(StdRedirection* r) {
    int fd;

    fflush(stdout);
    fflush(stderr);
    for (fd = 0; fd < 10; fd++) {
        if (r->saved[fd] >= 0) {
            _dup2(r->saved[fd], fd);
            drop_copy(r->saved[fd]);
            if (fd >= 3)
                fdOpen[fd] = 1;
        } else if (r->saved[fd] == -2) {
            _close(fd);                 // it was not open before
            fdOpen[fd] = 0;
        }
        r->saved[fd] = -1;
    }
}

void redirect_commit(StdRedirection* r) {
    int fd;

    fflush(stdout);
    fflush(stderr);
    for (fd = 0; fd < 10; fd++) {
        if (r->saved[fd] >= 0)
            drop_copy(r->saved[fd]);
        r->saved[fd] = -1;
    }
}

static int redirect_save(StdRedirection* r, int fd) {
    if (r->saved[fd] != -1)
        return 1;
    if (!fd_is_open(fd)) {
        r->saved[fd] = -2;
        return 1;
    }
    r->saved[fd] = copy_descriptor(fd);
    return r->saved[fd] >= 0;
}

/** Makes descriptor slot refer to the file descriptor fd (which is closed), and notes that it is open. */
static void redirect_take(int fd, int slot) {
    if (fd != slot) {
        _dup2(fd, slot);
        _close(fd);
    }
    if (slot >= 3)
        fdOpen[slot] = 1;
}

int redirect_begin(const Command* command, StdRedirection* r) {
    size_t i;
    int k;

    for (k = 0; k < 10; k++)
        r->saved[k] = -1;
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
        redirect_take(fd, 0);
        r->inputRedirected = 1;
    }

    for (i = 0; i < command->redirectCount; i++) {
        const Redirect* redirect = &command->redirects[i];
        int slot = redirect->fd;

        if (slot < 0 || slot > 9)
            continue;
        if (!redirect_save(r, slot)) {
            redirect_end(r);
            return -1;
        }
        if (slot == 0)
            r->inputRedirected = 1;
        if (redirect->file == NULL) {
            if (redirect->dupOf < 0) {
                // N>&-: a closed standard descriptor is NUL, as no program could be started with one
                if (slot < 3) {
                    int fd = _open("NUL", slot == 0 ? _O_RDONLY : _O_WRONLY);

                    if (fd >= 0)
                        redirect_take(fd, slot);
                } else if (fdOpen[slot]) {
                    _close(slot);
                    fdOpen[slot] = 0;
                }
            } else if (!fd_is_open(redirect->dupOf)) {
                fprintf(stderr, "Error: %d: Bad file descriptor\n", redirect->dupOf);
                redirect_end(r);
                return -1;
            } else if (redirect->dupOf != slot) {
                _dup2(redirect->dupOf, slot);
                if (slot >= 3)
                    fdOpen[slot] = 1;
            }
        } else if (redirect->input) {
            char native[VPATH_MAX];
            int fd = -1;

            if (vpath_to_native(redirect->file, native, sizeof native) == 0)
                fd = _open(native, _O_RDONLY | _O_BINARY);
            if (fd < 0) {
                fprintf(stderr, "Error: Cannot open '%s' for reading\n", redirect->file);
                redirect_end(r);
                return -1;
            }
            redirect_take(fd, slot);
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
            redirect_take(fd, slot);
        }
    }
    return 0;
}
