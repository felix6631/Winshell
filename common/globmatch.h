#ifndef GLOBMATCH_H
#define GLOBMATCH_H

/*
 * fnmatch without flags: ? * [set] [!set] [a-z] and the backslash, matched against the whole text, case counted.
 * Used by dircolors (the TERM lines) and by ls (--hide, -I).
 */

/** Non-zero when the text matches the pattern. */
int glob_match(const char* pattern, const char* text);

#endif //GLOBMATCH_H
