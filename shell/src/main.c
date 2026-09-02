#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "constants.h"


void winshell_terminalInit() {
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD prevMode;

    GetConsoleMode(hInput, &prevMode);

    // Do nothing now; 
}

int main() {
    winshell_terminalInit();
    CHAR input_buffer[BUFFERSIZE];
    while(1) {
        printf("~/ > ");
        fgets(input_buffer, BUFFERSIZE, stdin);
        // strip linefeed
        input_buffer[strlen(input_buffer)-1] = '\0';

        // quit command
        if (strcmp(input_buffer, "quit") == 0)
            break;

        puts(input_buffer);
    }
    return 0;
}