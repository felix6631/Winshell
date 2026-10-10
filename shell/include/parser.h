#ifndef PARSER_H
#define PARSER_H

#include <windows.h>

/**
 * winshell_parser splits the input line into words, using space, tab and
 * line feed as delimiters. The line is modified in place.
 *
 * It returns a NULL-terminated array of pointers into cmdline, so the number
 * of words is not limited. The array has to be freed with free(), and is
 * only valid as long as cmdline is.
 * An empty line yields an array holding just the terminating NULL.
 * NULL is returned only when memory allocation fails.
 */
LPSTR* winshell_parser(CHAR* cmdline);

#endif //PARSER_H
