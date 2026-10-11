#include <string.h>
#include <stdlib.h>
#include "winshellError.h"

int* strhex(char* string) {
    int* hex = (int*)malloc(sizeof(int) * (strlen(string)+1));
    for(size_t i = 0; i < strlen(string); i++) {
        hex[i] = (int)string[i];
    }
    hex[strlen(string)] = 0;
    return hex;
}

