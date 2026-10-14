#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "interp.h"
#include "parser.h"
#include "executer.h"
#include "builtin.h"
#include "cwd.h"
#include "variables.h"
#include "vpath.h"
#include "vec.h"

/* a pipeline: variables and wildcards are expanded, then it runs as a builtin or as programs */

static int run_item(const char* text, int* exitShell, int status) {
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
        // a builtin only makes sense as a command of its own, not inside a pipeline
        } else if (pipeline.count == 1 && builtin_run(&pipeline.commands[0], &builtinStatus, exitShell)) {
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
    CommandList list;
    size_t i;

    if (winshell_parse_list(line, &list) != 0) {
        winshell_setLastStatus(2);
        return 2;
    }

    // The commands of the line are run one after the other, each one parsed when its
    // turn has come: it sees the directory and the variables the one before left.
    for (i = 0; i < list.count && !*exitShell; i++) {
        int op = list.items[i].op;
        int run = op == LIST_FIRST || op == LIST_SEQ ||
                  (op == LIST_AND && status == 0) || (op == LIST_OR && status != 0);

        if (!run)
            continue;           // the status stays that of the last command that ran

        winshell_setLastStatus(status);
        status = run_item(list.items[i].text, exitShell, status);
        winshell_setLastStatus(status);
    }

    winshell_freeList(&list);
    return status;
}

/* ---- files ------------------------------------------------------------------------------ */

/** The parameters of a script as variables; the previous ones are kept to be put back. */
typedef struct {
    char* saved[13];            // 0..9, #, @, *
} Parameters;

static const char* const parameter_names[13] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "#", "@", "*" };

static void save_parameters(Parameters* p) {
    int i;

    for (i = 0; i < 13; i++) {
        const char* value = var_get(parameter_names[i]);
        p->saved[i] = value != NULL ? _strdup(value) : NULL;
    }
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
}

static void set_parameters(const char* name, const char* const* arguments, int count) {
    char number[16];
    size_t total = 1;
    char* all;
    int i;

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

    while (text[offset] != '\0' && !*exitShell) {
        size_t end = offset;
        size_t lineLength;
        char* line;

        while (text[end] != '\0' && text[end] != '\n')
            end++;
        lineLength = end - offset;
        if (lineLength > 0 && text[offset + lineLength - 1] == '\r')
            lineLength--;
        line = malloc(lineLength + 1);
        if (line == NULL) {
            fprintf(stderr, "Error: Out of memory\n");
            status = 1;
            break;
        }
        memcpy(line, text + offset, lineLength);
        line[lineLength] = '\0';
        status = interp_run_line(line, exitShell, status);
        free(line);
        offset = text[end] == '\n' ? end + 1 : end;
    }

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
static int substitute(const char* command, char** output) {
    HANDLE read = NULL, write = NULL;
    Capture capture;
    HANDLE thread;
    int pipeFd, savedOut, exitSub = 0, status, before;
    char directory[VPATH_MAX];
    VarSnapshot* variables;

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
    before = winshell_lastStatus();
    status = interp_run_line(command, &exitSub, before);
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
    return 0;
}

void interp_init(void) {
    winshell_setSubstitution(substitute);
}
