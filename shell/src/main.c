#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "constants.h"
#include "parser.h"
#include "executer.h"

void winshell_terminalInit() {
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD prevMode;

    GetConsoleMode(hInput, &prevMode);

    // Do nothing now;
}

/**
 * Reads one whole line from stdin, however long it is.
 * Returns NULL at the end of input. The result has to be freed.
 */
static char* read_line(void) {
    size_t length = 0, capacity = BUFFERSIZE;
    char* line = malloc(capacity);
    if (line == NULL)
        return NULL;

    for (;;) {
        if (fgets(line + length, (int)(capacity - length), stdin) == NULL) {
            if (length == 0) {
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

        // end of input
        char* input = read_line();
        if (input == NULL)
            break;

        // strip linefeed
        input[strcspn(input, "\r\n")] = '\0';

        // quit command
        if (strcmp(input, "quit") == 0) {
            free(input);
            break;
        }

        LPSTR* parsed = winshell_parser(input);
        if (parsed == NULL) {
            fprintf(stderr, "Error: Out of memory\n");
            free(input);
            continue;
        }

        // execute given command
        if (parsed[0] != NULL) {
            int result = execute(parsed[0], parsed);
            if (result == -1) {
                fprintf(stderr, "Error: Failed to execute command '%s'\n", parsed[0]);
            }
        }

        free(parsed);
        free(input);
    }
    return 0;
}
