#ifndef PARSER_H
#define PARSER_H

#include <windows.h>

/**
 * winshell_parser parses input line into words, using space linefeed as delimiter.
 * It marks endpoint with a pointer of -1.
 */
LPSTR* winshell_parser(CHAR* cmdline);

#endif //PARSER_H