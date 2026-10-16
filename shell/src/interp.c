#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "interp.h"
#include "jobs.h"
#include "command.h"
#include "parser.h"
#include "executer.h"
#include "builtin.h"
#include "cwd.h"
#include "variables.h"
#include "vpath.h"
#include "vec.h"

/* a pipeline: variables and wildcards are expanded, then it runs as a builtin or as programs */

int interp_run_simple(const char* text, int* exitShell, int status) {
    Pipeline pipeline;
    unsigned long substitutionsBefore = interp_substitution_count();

    // the parser and the executer report their own errors
    if (winshell_parse(text, &pipeline) != 0)
        return 2;

    if (pipeline.count > 0 && winshell_glob(&pipeline) == 0) {
        int builtinStatus = status;

        // NAME=value alone sets variables of the shell; only that command does, not one inside a pipeline
        if (pipeline.count == 1 && pipeline.commands[0].argv == NULL) {
            const Command* command = &pipeline.commands[0];
            size_t i;

            for (i = 0; i < command->assignmentCount; i++) {
                const char* assignment = command->assignments[i];
                size_t length = strcspn(assignment, "=");
                char name[256];

                if (length < sizeof name) {
                    memcpy(name, assignment, length);
                    name[length] = '\0';
                    if (var_set(name, assignment + length + 1) != 0)
                        fprintf(stderr, "Error: Out of memory\n");
                } else {
                    fprintf(stderr, "Error: Variable name too long\n");
                }
            }
            // redirections still take effect (> file empties it)
            status = execute_pipeline(&pipeline) == 0 ? 0 : 1;
            // x=$(command) has the status of the command
            if (status == 0 && interp_substitution_count() != substitutionsBefore)
                status = interp_substitution_status();
        // kill %1: the job is its processes
        } else if (pipeline.count == 1 && pipeline.commands[0].argv != NULL &&
                   strcmp(pipeline.commands[0].argv[0], "kill") == 0 && jobs_has_job_argument(pipeline.commands[0].argv)) {
            Command* command = &pipeline.commands[0];
            char** expanded = jobs_expand_kill_arguments(command->argv);

            if (expanded == NULL) {
                status = 1;
            } else {
                LPSTR* original = command->argv;
                int result;

                command->argv = expanded;
                result = execute_pipeline(&pipeline);
                command->argv = original;
                jobs_free_arguments(expanded, (size_t)-1);
                status = result == -1 ? 127 : result;
            }
        // a builtin only makes sense as a command of its own, not inside a pipeline
        } else if (pipeline.count == 1 && pipeline.commands[0].argv != NULL &&
                   (builtin_exists(pipeline.commands[0].argv[0]) ||
                    command_has_function(pipeline.commands[0].argv[0]))) {
            // NAME=value in front of a builtin holds for the builtin only (IFS=: read a b)
            const Command* command = &pipeline.commands[0];
            char* old[16];
            size_t i, count = command->assignmentCount < 16 ? command->assignmentCount : 16;

            for (i = 0; i < count; i++) {
                const char* assignment = command->assignments[i];
                size_t length = strcspn(assignment, "=");
                char name[256];
                const char* previous;

                memcpy(name, assignment, length < sizeof name ? length : sizeof name - 1);
                name[length < sizeof name ? length : sizeof name - 1] = '\0';
                previous = var_get(name);
                old[i] = previous != NULL ? _strdup(previous) : NULL;
                var_set(name, assignment + length + 1);
            }
            {
                // redirections of a builtin (read x < file, cd > log) are in force while it runs
                StdRedirection saved;
                int redirected = command->input != NULL || command->redirectCount > 0;

                if (redirected && redirect_begin(command, &saved) != 0) {
                    builtinStatus = 1;
                } else {
                    if (redirected && saved.inputRedirected)
                        interp_input_redirected(1);
                    if (command_has_function(command->argv[0]))
                        builtinStatus = command_call_function(command->argv[0], command->argv, exitShell, status);
                    else
                        builtin_run(command, &builtinStatus, exitShell);
                    if (redirected && saved.inputRedirected)
                        interp_input_redirected(-1);
                    if (redirected)
                        redirect_end(&saved);
                }
            }
            for (i = 0; i < count; i++) {
                const char* assignment = command->assignments[i];
                size_t length = strcspn(assignment, "=");
                char name[256];

                memcpy(name, assignment, length < sizeof name ? length : sizeof name - 1);
                name[length < sizeof name ? length : sizeof name - 1] = '\0';
                if (old[i] != NULL)
                    var_set(name, old[i]);
                else
                    var_unset(name);
                free(old[i]);
            }
            status = builtinStatus;
        } else {
            int result = execute_pipeline(&pipeline);
            // a program that could not be started is a command that was not found
            status = result == -1 ? 127 : result;
        }
    } else if (pipeline.count > 0) {
        status = 1;
    }

    winshell_freePipeline(&pipeline);
    return status;
}

int interp_run_line(const char* line, int* exitShell, int status) {
    return command_run_text(line, exitShell, status);
}

/* ---- files ------------------------------------------------------------------------------ */

/** The parameters of a script as variables; the previous ones are kept to be put back. */
typedef struct {
    char* saved[13];            // 0..9, #, @, *
    Positionals* list;          // the parameters as a list
} Parameters;

static const char* const parameter_names[13] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "#", "@", "*" };

static void set_parameter_values(const char* name, const char* const* arguments, int count);

static void save_parameters(Parameters* p) {
    int i;

    for (i = 0; i < 13; i++) {
        const char* value = var_get(parameter_names[i]);
        p->saved[i] = value != NULL ? _strdup(value) : NULL;
    }
    p->list = winshell_positionals_save();
}

static void restore_parameters(Parameters* p) {
    int i;

    for (i = 0; i < 13; i++) {
        if (p->saved[i] != NULL)
            var_set(parameter_names[i], p->saved[i]);
        else
            var_unset(parameter_names[i]);
        free(p->saved[i]);
    }
    winshell_positionals_restore(p->list);
    p->list = NULL;
}

static void set_parameters(const char* name, const char* const* arguments, int count) {
    set_parameter_values(name, arguments, count);
}

void interp_set_parameters(const char* name, const char* const* arguments, int count) {
    set_parameter_values(name, arguments, count);
}

struct ParameterFrame {
    Parameters saved;
};

ParameterFrame* interp_parameters_push(const char* const* arguments, int count) {
    ParameterFrame* frame = calloc(1, sizeof *frame);

    if (frame == NULL)
        return NULL;
    save_parameters(&frame->saved);
    set_parameter_values(NULL, arguments, count);
    return frame;
}

void interp_parameters_pop(ParameterFrame* frame) {
    if (frame == NULL)
        return;
    restore_parameters(&frame->saved);
    free(frame);
}

static void set_parameter_values(const char* name, const char* const* arguments, int count) {
    char number[16];
    size_t total = 1;
    char* all;
    int i;

    if (name != NULL)
        var_set("0", name);
    for (i = 1; i <= 9; i++) {
        if (i <= count) {
            var_set(parameter_names[i], arguments[i - 1]);
        } else {
            var_unset(parameter_names[i]);
        }
    }
    snprintf(number, sizeof number, "%d", count);
    var_set("#", number);

    for (i = 0; i < count; i++)
        total += strlen(arguments[i]) + 1;
    all = malloc(total);
    if (all != NULL) {
        size_t used = 0;
        for (i = 0; i < count; i++)
            used += (size_t)sprintf(all + used, "%s%s", i > 0 ? " " : "", arguments[i]);
        all[used] = '\0';
        var_set("@", all);
        var_set("*", all);
        free(all);
    }
    winshell_positionals_set(arguments, count);     // last: the arguments may be the strings of the old list
}

int interp_run_file(const char* path, const char* const* arguments, int argumentCount, int setParameters,
                    int* exitShell, int status) {
    char native[VPATH_MAX];
    FILE* file;
    char* text = NULL;
    size_t length = 0, capacity = 0, offset = 0;
    Parameters saved;
    int got;

    if (vpath_to_native(path, native, sizeof native) != 0 || (file = fopen(native, "rb")) == NULL) {
        fprintf(stderr, "shell: %s: No such file or directory\n", path);
        return 127;
    }
    for (;;) {
        if (VEC_RESERVE(text, capacity, length + 4097) != 0) {
            fclose(file);
            free(text);
            fprintf(stderr, "Error: Out of memory\n");
            return 1;
        }
        got = (int)fread(text + length, 1, 4096, file);
        if (got <= 0)
            break;
        length += (size_t)got;
    }
    fclose(file);
    text[length] = '\0';
    if (length >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        offset = 3;                                 // a UTF-8 byte order mark

    memset(&saved, 0, sizeof saved);
    if (setParameters) {
        save_parameters(&saved);
        set_parameters(path, arguments, argumentCount);
    }

    // CR LF line ends are line ends
    {
        size_t from, to = offset;

        for (from = offset; text[from] != '\0'; from++)
            if (!(text[from] == '\r' && text[from + 1] == '\n'))
                text[to++] = text[from];
        text[to] = '\0';
    }
    status = command_run_text(text + offset, exitShell, status);

    if (setParameters)
        restore_parameters(&saved);
    free(text);
    return status;
}

/* ---- command substitution --------------------------------------------------------------- */

static unsigned long substitutionCount;
static int substitutionStatus;

unsigned long interp_substitution_count(void) {
    return substitutionCount;
}

int interp_substitution_status(void) {
    return substitutionStatus;
}

typedef struct {
    HANDLE read;
    char*  data;
    size_t length, capacity;
} Capture;

static DWORD WINAPI capture_thread(LPVOID parameter) {
    Capture* c = parameter;
    char chunk[4096];
    DWORD got;

    while (ReadFile(c->read, chunk, sizeof chunk, &got, NULL) && got > 0) {
        if (VEC_RESERVE(c->data, c->capacity, c->length + got + 1) != 0)
            continue;                               // out of memory: the rest is dropped, the pipe still drained
        memcpy(c->data + c->length, chunk, got);
        c->length += got;
    }
    return 0;
}

/**
 * Runs a line with its standard output going to a pipe, and returns what was written. It runs in a
 * subshell as far as that can be done in the process: the directory and the variables are what they were
 * before when it has ended, and an `exit` ends only the substitution.
 */
int interp_capture_call(int (*run)(void* context), void* context, char** output, size_t* outputLength, int* statusOut) {
    HANDLE read = NULL, write = NULL;
    Capture capture;
    HANDLE thread;
    int pipeFd, savedOut, status, before;
    char directory[VPATH_MAX];
    VarSnapshot* variables;
    void* traps;
    Positionals* positionals;

    *output = NULL;
    memset(&capture, 0, sizeof capture);
    if (!CreatePipe(&read, &write, NULL, 0))
        return -1;
    capture.read = read;

    pipeFd = _open_osfhandle((intptr_t)write, _O_BINARY | _O_WRONLY);
    if (pipeFd < 0) {
        CloseHandle(read);
        CloseHandle(write);
        return -1;
    }
    thread = CreateThread(NULL, 0, capture_thread, &capture, 0, NULL);
    if (thread == NULL) {
        _close(pipeFd);
        CloseHandle(read);
        return -1;
    }

    fflush(stdout);
    savedOut = _dup(1);
    _dup2(pipeFd, 1);                               // also the standard output handle, which programs inherit
    _close(pipeFd);

    cwd_get(directory, sizeof directory);
    variables = var_save();
    traps = command_traps_enter_subshell();
    positionals = winshell_positionals_save();
    before = winshell_lastStatus();
    status = run(context);
    command_traps_leave_subshell(traps);
    winshell_positionals_restore(positionals);
    winshell_setLastStatus(before);                 // $? of the line around the substitution is not changed

    fflush(stdout);
    if (variables != NULL) {
        char absolute[VPATH_MAX];
        cwd_change(directory, absolute, sizeof absolute);
        var_restore(variables);
    }
    _dup2(savedOut, 1);                             // the pipe has no writer left: the thread sees the end
    _close(savedOut);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    CloseHandle(read);

    substitutionCount++;
    substitutionStatus = status;
    if (capture.data == NULL) {
        capture.data = malloc(1);
        if (capture.data == NULL)
            return -1;
    }
    capture.data[capture.length] = '\0';
    *output = capture.data;
    if (outputLength != NULL)
        *outputLength = capture.length;
    if (statusOut != NULL)
        *statusOut = status;
    return 0;
}

static int run_captured_line(void* context) {
    int exitSub = 0;

    return interp_run_line((const char*)context, &exitSub, winshell_lastStatus());
}

int interp_capture(const char* command, char** output, size_t* outputLength, int* statusOut) {
    return interp_capture_call(run_captured_line, (void*)command, output, outputLength, statusOut);
}

static int substitute(const char* command, char** output) {
    return interp_capture(command, output, NULL, NULL);
}

/* ---- the input of the shell: read, and what a compound command is fed --------------------- */

static int inputRedirected;
static char* (*lineReader)(void);

void interp_input_redirected(int delta) {
    inputRedirected += delta;
}

void interp_set_line_reader(char* (*reader)(void)) {
    lineReader = reader;
}

int interp_read_line(char** line) {
    size_t length = 0, capacity = 0;
    char* text = NULL;

    *line = NULL;
    if (inputRedirected == 0) {
        char* input = lineReader != NULL ? lineReader() : NULL;
        size_t n;

        if (input == NULL)
            return 0;
        n = strcspn(input, "\r\n");
        input[n] = '\0';
        *line = input;
        return 1;
    }

    // standard input was replaced (`done < file`, a pipe into a loop): the bytes come from it, one by one,
    // so that what is read stays where the programs started in the loop would continue
    for (;;) {
        char c;
        int got = _read(0, &c, 1);

        if (got <= 0) {
            if (length == 0) {
                free(text);
                return 0;
            }
            break;
        }
        if (c == '\n')
            break;
        if (VEC_RESERVE(text, capacity, length + 2) != 0) {
            free(text);
            return 0;
        }
        text[length++] = c;
    }
    if (text == NULL) {
        text = malloc(1);
        if (text == NULL)
            return 0;
    }
    if (length > 0 && text[length - 1] == '\r')
        length--;
    text[length] = '\0';
    *line = text;
    return 1;
}

struct InputRedirection {
    int  saved;
    char path[MAX_PATH];
};

InputRedirection* interp_input_from_bytes(const char* data, size_t length) {
    InputRedirection* r = calloc(1, sizeof *r);
    char directory[MAX_PATH];
    FILE* file;
    int fd;

    if (r == NULL)
        return NULL;
    if (GetTempPathA(sizeof directory, directory) == 0 || GetTempFileNameA(directory, "wsh", 0, r->path) == 0) {
        free(r);
        return NULL;
    }
    file = fopen(r->path, "wb");
    if (file == NULL) {
        free(r);
        return NULL;
    }
    if (length > 0 && fwrite(data, 1, length, file) != length) {
        fclose(file);
        DeleteFileA(r->path);
        free(r);
        return NULL;
    }
    fclose(file);
    fd = _open(r->path, _O_RDONLY | _O_BINARY | _O_TEMPORARY);
    if (fd < 0) {
        DeleteFileA(r->path);
        free(r);
        return NULL;
    }
    r->saved = _dup(0);
    _dup2(fd, 0);
    _close(fd);
    return r;
}

void interp_input_restore(InputRedirection* r) {
    if (r == NULL)
        return;
    if (r->saved >= 0) {
        _dup2(r->saved, 0);
        _close(r->saved);
    }
    DeleteFileA(r->path);               // gone already when it was opened as a temporary file
    free(r);
}

void interp_init(void) {
    winshell_setSubstitution(substitute);
}
