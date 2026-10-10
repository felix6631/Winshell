#ifndef PARSER_H
#define PARSER_H

#include <windows.h>

/**
 * One command of a pipeline: the program with its arguments and the
 * redirections written next to it.
 */
typedef struct {
    LPSTR* argv;        // NULL-terminated; argv[0] is the program name
    LPSTR  input;       // "< file"   or NULL
    LPSTR  output;      // "> file" / ">> file"   or NULL
    int    append;      // non-zero for ">>"
    LPSTR  errput;      // "2> file" / "2>> file"   or NULL
    int    errAppend;   // non-zero for "2>>"
} Command;

/** Commands connected with '|'. A blank line gives a pipeline of zero commands. */
typedef struct {
    Command* commands;
    size_t   count;
    LPSTR    buffer;    // owns the text the commands point into
} Pipeline;

/**
 * winshell_parse splits the input line into a pipeline. The line is not modified.
 *
 * Words are separated by space, tab and line feed. These are operators:
 *   a | b     the output of a becomes the input of b
 *   < file    read standard input from file
 *   > file    write standard output to file, replacing it
 *   >> file   append standard output to file
 *   2> file   write standard error to file, replacing it
 *   2>> file  append standard error to file
 * Operators need no surrounding spaces: "a>b|c" works.
 *
 * Quoting (operators lose their meaning inside quotes):
 *   'text'   everything inside is taken literally
 *   "text"   spaces are kept; \" gives a literal double quote
 *   Quoted parts can be mixed with unquoted ones: a"b c"d is one word "ab cd".
 *   "" and '' give an empty word.
 *   Backslashes are only special in front of a double quote (as in
 *   CommandLineToArgvW), so Windows paths such as C:\bin\echo work as typed.
 *
 * Returns 0 on success. On error (unclosed quote, missing command or file
 * name, out of memory) a message is printed, -1 is returned and nothing has
 * to be freed. A successful result has to be released with
 * winshell_freePipeline.
 */
int winshell_parse(const CHAR* line, Pipeline* pipeline);

void winshell_freePipeline(Pipeline* pipeline);

#endif //PARSER_H
