#include <string.h>
#include <stdlib.h>
#include "parser.h"
#include "constants.h"
#include "winshellError.h"
#include <wtypesbase.h>

#include <stdio.h>

LPSTR* winshell_parser(CHAR* cmdline) {
    DWORD length = 0, capacity = 4;

    LPSTR *parsed = calloc(capacity, sizeof *parsed);
    
    parsed[length++] = strtok(cmdline, " \n");
    if (parsed[0] == NULL)
        return parsed;
    
    

    while ((parsed[length++] = strtok(NULL, " \n"))) {
        if (length >= PARSEDSIZE) {
            goto err_parseLimitExceed;
            return NULL;
        }
        if (length >= capacity) {
            capacity *= 2;
            parsed = realloc(parsed, capacity * sizeof(*parsed));
        }
    }

    for(size_t i = length; i < capacity; i++)
        parsed[i] = NULL;
    
    return parsed;

err_parseLimitExceed:
    fprintf(stderr, "Error: Parsed limit exceeded. Max limit is %d\n", PARSEDSIZE);
    free(parsed);
    return NULL;
}