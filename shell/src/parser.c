#include <string.h>
#include <stdlib.h>
#include "parser.h"
#include <windef.h>

#define DELIMITERS " \t\r\n"

LPSTR* winshell_parser(CHAR* cmdline) {
    size_t length = 0, capacity = 8;
    LPSTR* parsed = malloc(capacity * sizeof *parsed);
    if (parsed == NULL)
        return NULL;

    for (LPSTR word = strtok(cmdline, DELIMITERS); word != NULL; word = strtok(NULL, DELIMITERS)) {
        // keep room for this word and the terminating NULL
        if (length + 2 > capacity) {
            capacity *= 2;
            LPSTR* grown = realloc(parsed, capacity * sizeof *parsed);
            if (grown == NULL) {
                free(parsed);
                return NULL;
            }
            parsed = grown;
        }
        parsed[length++] = word;
    }

    parsed[length] = NULL;
    return parsed;
}
