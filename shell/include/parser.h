#ifndef PARSER_H
#define PARSER_H

#include <windows.h>

/**
 * In a glob pattern, this character in front of another one makes that one
 * an ordinary character: the quoted "*" of  echo "*"  is stored as
 * GLOB_ESCAPE followed by '*'.
 */
#define GLOB_ESCAPE '\001'

/**
 * One command of a pipeline: the program with its arguments and the
 * redirections written next to it.
 */
typedef struct {
    LPSTR* argv;        // NULL-terminated; argv[0] is the program name
    LPSTR* patterns;    // same length as argv: the glob pattern of a word, or NULL
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
    LPSTR    patterns;  // owns the text of the glob patterns
    LPSTR*   owned;     // strings created by glob expansion, which argv points into
    size_t   ownedCount;
    size_t   ownedCapacity;
} Pipeline;

/**
 * The status the last command ended with, which $? is. The shell sets it
 * after each command of a line.
 */
void winshell_setLastStatus(int status);

/** How a command of a list depends on the one before it. */
#define LIST_FIRST 0    // the first command of the line
#define LIST_SEQ   1    // ;   always runs
#define LIST_AND   2    // &&  runs when the one before succeeded
#define LIST_OR    3    // ||  runs when the one before failed

typedef struct {
    char* text;         // the command line of one pipeline
    int   op;           // how it is connected to the previous one
} ListItem;

typedef struct {
    ListItem* items;
    size_t    count;
} CommandList;

/**
 * winshell_parse_list cuts a line into pipelines at ;  &&  and ||  outside of
 * quotes. Each item is parsed with winshell_parse when its turn has come,
 * because what a command does can change what the next one reads: the
 * current directory, the variables.
 * An empty line gives an empty list; a ; at the end is fine.
 * Returns 0, or -1 after a message for a missing command, a lone '&' (not
 * supported), or out of memory.
 */
int winshell_parse_list(const CHAR* line, CommandList* list);

void winshell_freeList(CommandList* list);

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
 * Quoting (operators and wildcards lose their meaning inside quotes):
 *   'text'   everything inside is taken literally
 *   "text"   spaces are kept; \" gives a literal double quote
 *   Quoted parts can be mixed with unquoted ones: a"b c"d is one word "ab cd".
 *   "" and '' give an empty word.
 *   Backslashes are only special in front of a double quote (as in
 *   CommandLineToArgvW), so Windows paths such as C:\bin\echo work as typed.
 *
 * Variables: $NAME, ${NAME} and $? are replaced by their values, in double
 * quotes as well, but not in single quotes. In double quotes the value stays
 * one word, as it does in the quotes of "$HOME"; outside of them it is cut at
 * blanks into words, which are then subject to wildcards. A variable that is
 * not set is nothing: it adds no word when it stands alone. ~ at the start of
 * a word, alone or in front of a / or \, is $HOME. A $ that starts no name is
 * an ordinary character. A backslash is no escape for the $; use single quotes.
 *
 * Wildcards: a word with an unquoted * ? or [...] is a glob pattern.
 * The parser only records it, in Command.patterns, next to the word as it
 * reads without wildcards; winshell_glob expands it. The pattern is the
 * word's text with GLOB_ESCAPE in front of each quoted * ? and [.
 *
 * Returns 0 on success. On error (unclosed quote, missing command or file
 * name, out of memory) a message is printed, -1 is returned and nothing has
 * to be freed. A successful result has to be released with
 * winshell_freePipeline.
 */
int winshell_parse(const CHAR* line, Pipeline* pipeline);

/**
 * Replaces the words that have a glob pattern by the paths that match it.
 * It is meant to be called after winshell_parse, in the shell that owns the
 * current directory. See glob.h.
 * Returns 0, or -1 when out of memory (a message is printed).
 */
int winshell_glob(Pipeline* pipeline);

void winshell_freePipeline(Pipeline* pipeline);

#endif //PARSER_H
