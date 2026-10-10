#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "constants.h"
#include "parser.h"
#include "executer.h"
#include "builtin.h"

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

void winshell_terminalInit() {
    SetConsoleCtrlHandler(winshell_ctrlHandler, TRUE);
    builtin_init();
}

/**
 * Reads one whole line from stdin, however long it is.
 * Returns NULL at the end of input, or when reading was interrupted.
 * The result has to be freed.
 */
static char* read_line(void) {
    size_t length = 0, capacity = BUFFERSIZE;
    char* line = malloc(capacity);
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

        length += strlen(line + length);
        if (line[length - 1] == '\n')
            break;

        // buffer is full without a line feed; make room for the rest
        if (length + 1 >= capacity) {
            char* grown = realloc(line, capacity * 2);
            if (grown == NULL) {
                free(line);
                return NULL;
            }
            line = grown;
            capacity *= 2;
        }
    }
    return line;
}

int main() {
    winshell_terminalInit();
    while(1) {
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

        // quit command
        if (strcmp(input, "quit") == 0) {
            free(input);
            break;
        }

        // the parser and the executer report their own errors
        Pipeline pipeline;
        if (winshell_parse(input, &pipeline) == 0) {
            // a builtin only makes sense as a command of its own, not inside a pipeline
            if (pipeline.count == 0 || pipeline.count > 1 || !builtin_run(&pipeline.commands[0]))
                execute_pipeline(&pipeline);
            winshell_freePipeline(&pipeline);
        }

        free(input);
    }
    return 0;
}
