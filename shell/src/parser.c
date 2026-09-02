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
            // TODO: Raise Exception Here
            return NULL;
        }
        if (length >= capacity) {
            capacity *= 2;
            parsed = realloc(parsed, capacity * sizeof *parsed);
        }
    }

    

    for(size_t i = length; i < capacity; i++)
        parsed[i] = NULL;


    
    // for(size_t i = 0; i < capacity; i++) {
    //     int* hex = strhex(parsed[i]);
    //     for(int j = 0; hex[j] != 0; j++)
    //         printf("%x ",hex[j]);
    //     printf("\n");
    // }
    

    return parsed;
}