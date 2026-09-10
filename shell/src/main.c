#include <stdio.h>
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

int main() {
    winshell_terminalInit();
    CHAR input_buffer[BUFFERSIZE];
    while(1) {
        memset(input_buffer, 0, sizeof(CHAR));
        printf(" > ");
        fgets(input_buffer, BUFFERSIZE, stdin);
        // strip linefeed
        input_buffer[strlen(input_buffer)-1] = '\0';

        // quit command
        if (strcmp(input_buffer, "quit") == 0)
            break;

        LPSTR* parsed = winshell_parser(input_buffer);
        
        // execute given command
        if (parsed[0] != NULL) {
            int result = execute(parsed[0], parsed[1], parsed[2], parsed[3], parsed[4]);
            if (result != 0) {
                fprintf(stderr, "Error: Failed to execute command '%s'\n", parsed[0]);
            }
        }
    }
    return 0;
}