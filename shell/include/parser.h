#ifndef PARSER_H
#define PARSER_H

#include <windows.h>

/**
 * winshell_parser splits the input line into words, using space, tab and
 * line feed as delimiters. The line is modified in place.
 *
 * Quoting:
 *   'text'   everything inside is taken literally
 *   "text"   spaces are kept; \" gives a literal double quote
 *   Quoted parts can be mixed with unquoted ones: a"b c"d is one word "ab cd".
 *   "" and '' give an empty word.
 *   Backslashes are only special in front of a double quote (as in
 *   CommandLineToArgvW), so Windows paths such as C:\bin\echo work as typed.
 *
 * It returns a NULL-terminated array of pointers into cmdline, so the number
 * of words is not limited. The array has to be freed with free(), and is
 * only valid as long as cmdline is.
 * An empty line yields an array holding just the terminating NULL.
 *
 * NULL is returned, after an error message was printed, when a quote is not
 * closed or memory allocation fails.
 */
LPSTR* winshell_parser(CHAR* cmdline);

#endif //PARSER_H
