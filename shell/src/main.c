#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "parser.h"
#include "executer.h"
#include "builtin.h"
#include "cwd.h"
#include "variables.h"
#include "vec.h"

/** Set when Ctrl+C or Ctrl+Break reached the shell. */
static volatile LONG interrupted = 0;

/**
 * Ctrl+C and Ctrl+Break are delivered to every process on the console.
 * The shell swallows them so that it survives, while the program it is
 * running has its own default handler and is terminated.
 * Closing the console, logging off and shutting down are left to the default handler.
 *
 * SetConsoleCtrlHandler(NULL, TRUE) is deliberately not used: that setting
 * would be inherited, and the programs started by the shell could not be interrupted.
 */
static BOOL WINAPI winshell_ctrlHandler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            InterlockedExchange(&interrupted, 1);
            return TRUE;
        default:
            return FALSE;
    }
}

/* the code pages of the console before the shell changed them */
static UINT savedInputCodePage, savedOutputCodePage;

static void restore_console_code_pages(void) {
    if (savedInputCodePage != 0)
        SetConsoleCP(savedInputCodePage);
    if (savedOutputCodePage != 0)
        SetConsoleOutputCP(savedOutputCodePage);
}

/**
 * The programs of the shell write UTF-8 (see common/utf8.manifest), and so do the lines it reads: the
 * console is put into UTF-8 for as long as the shell runs, and given back at the end.
 */
static void use_utf8_console(void) {
    DWORD mode;

    if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) || GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode)) {
        savedInputCodePage = GetConsoleCP();
        savedOutputCodePage = GetConsoleOutputCP();
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
        atexit(restore_console_code_pages);
    }
}

static void winshell_terminalInit(void) {
    SetConsoleCtrlHandler(winshell_ctrlHandler, TRUE);
    use_utf8_console();
    cwd_init();
}

/**
 * One line typed at the console, as UTF-8 with its line ending. The wide function is used so that
 * what is typed (an IME composes Korean, for one) arrives whole whatever the code page is.
 * Returns NULL at the end of input (Ctrl+Z) and when Ctrl+C interrupted the reading.
 */
static char* read_console_line(HANDLE in) {
    wchar_t* wide = NULL;
    size_t length = 0, capacity = 0;
    char* line;
    int bytes;

    for (;;) {
        wchar_t chunk[512];
        DWORD got = 0;

        if (!ReadConsoleW(in, chunk, 512, &got, NULL) || got == 0) {
            free(wide);
            return NULL;
        }
        if (VEC_RESERVE(wide, capacity, length + got + 1) != 0) {
            free(wide);
            return NULL;
        }
        memcpy(wide + length, chunk, got * sizeof *chunk);
        length += got;
        if (chunk[got - 1] == L'\n')
            break;
    }

    // Ctrl+Z at the start of a line is the end of the input
    if (wide[0] == 0x1A) {
        free(wide);
        return NULL;
    }

    bytes = WideCharToMultiByte(CP_UTF8, 0, wide, (int)length, NULL, 0, NULL, NULL);
    line = malloc((size_t)bytes + 1);
    if (line != NULL) {
        WideCharToMultiByte(CP_UTF8, 0, wide, (int)length, line, bytes, NULL, NULL);
        line[bytes] = '\0';
    }
    free(wide);
    return line;
}

/**
 * Reads one whole line from stdin, however long it is.
 * Returns NULL at the end of input, or when reading was interrupted.
 * The result has to be freed.
 */
static char* read_line(void) {
    size_t length = 0, capacity = BUFSIZ;
    char* line;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    if (in != INVALID_HANDLE_VALUE && GetConsoleMode(in, &mode))
        return read_console_line(in);

    line = malloc(capacity);
    if (line == NULL)
        return NULL;

    for (;;) {
        if (fgets(line + length, (int)(capacity - length), stdin) == NULL) {
            if (length == 0 || interrupted) {
                free(line);
                return NULL;
            }
            break;
        }

        {
            size_t got = strlen(line + length);
            if (got == 0)
                continue;       // a NUL byte: nothing to keep, and there is no last character to look at
            length += got;
        }
        if (line[length - 1] == '\n')
            break;

        // buffer is full without a line feed; make room for the rest
        if (length + 1 >= capacity && VEC_RESERVE(line, capacity, capacity + 1) != 0) {
            free(line);
            return NULL;
        }
    }
    return line;
}

/**
 * Runs one pipeline: variables and wildcards are expanded, then the command is
 * run, as a builtin or as the programs of the pipeline. Returns its status.
 * *exitShell is set when the shell has to end.
 */
static int run_item(const char* text, int* exitShell, int status) {
    Pipeline pipeline;

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

int main(void) {
    int status = 0, exitShell = 0;

    winshell_terminalInit();
    while(!exitShell) {
        CommandList list;
        size_t i;

        printf(" > ");
        fflush(stdout);

        InterlockedExchange(&interrupted, 0);
        char* input = read_line();
        if (input == NULL) {
            // Ctrl+C at the prompt abandons the line, it does not end the shell
            if (interrupted) {
                InterlockedExchange(&interrupted, 0);
                clearerr(stdin);
                putchar('\n');
                continue;
            }
            // end of input
            break;
        }

        // strip linefeed
        input[strcspn(input, "\r\n")] = '\0';

        if (winshell_parse_list(input, &list) != 0) {
            status = 2;
            winshell_setLastStatus(status);
            free(input);
            continue;
        }

        // The commands of the line are run one after the other, each one parsed when its
        // turn has come: it sees the directory and the variables the one before left.
        for (i = 0; i < list.count && !exitShell; i++) {
            int op = list.items[i].op;
            int run = op == LIST_FIRST || op == LIST_SEQ ||
                      (op == LIST_AND && status == 0) || (op == LIST_OR && status != 0);

            if (!run)
                continue;           // the status stays that of the last command that ran

            winshell_setLastStatus(status);
            status = run_item(list.items[i].text, &exitShell, status);
            winshell_setLastStatus(status);
        }

        winshell_freeList(&list);
        free(input);
    }
    return status;
}
