#ifndef BRACE_H
#define BRACE_H

/**
 * Brace expansion, done on the text of a command before anything else is expanded:
 *
 *     a{b,c}d        ->  abd acd
 *     {1..4}         ->  1 2 3 4        {01..03} -> 01 02 03      {a..e..2} -> a c e      {5..1} -> 5 4 3 2 1
 *     x{a,b{1,2}}y   ->  xay xb1y xb2y
 *     {a,b}{1,2}     ->  a1 a2 b1 b2
 *
 * A brace expression needs a comma at its top level, or a sequence x..y[..step] of two numbers or two letters;
 * anything else (`{}`, `{a}`, `{ list; }`) stays as it is. Quoted braces, braces in ${...} and $(...), the value
 * of an assignment (NAME={a,b}) and the word after a redirection are not expanded.
 *
 * Returns 0 when the line has nothing to expand (*result is NULL), 1 with the new line in *result (malloc'ed:
 * the words that were expanded are separated by a space), or -1 when out of memory.
 */
int winshell_brace_expand(const char* line, char** result);

/** Switches the expansion off (the patterns of a case are not expanded) and on again. */
void winshell_brace_enable(int on);

#endif //BRACE_H
