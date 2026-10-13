#ifndef REGEX_H
#define REGEX_H

#include <stddef.h>
#include <stdint.h>

/*
 * Regular expressions for grep (and whatever comes next): POSIX basic and extended syntax
 * with the GNU additions, matched as the GNU tools do it, the leftmost match being the
 * longest one.
 *
 *   both     . [set] [^set] [[:class:]] ^ $ * \< \> \b \B \w \W \s \S \` \' \1..\9
 *   basic    \( \) \{m,n\} \| \+ \?          (a * at the start of an expression is a literal)
 *   extended ( ) {m,n} | + ?
 *
 * Text is UTF-8: a character is a whole sequence, "." matches one of them, a bracket
 * expression may hold any of them. A byte that is not part of a valid sequence matches only
 * itself (so ".", sets and classes never match it).
 *
 * The matching works on decoded text (RegexInput), which is made once for a line and can be
 * searched any number of times. Positions are indexes of characters; input->offset turns
 * them into byte offsets.
 */

typedef struct Regex Regex;

#define REGEX_EXTENDED   1      /* ERE instead of BRE */
#define REGEX_ICASE      2      /* ignore case (ASCII and what towlower knows) */
#define REGEX_FIXED      4      /* the patterns are plain strings */
#define REGEX_WORD       8      /* a match has to be a whole word (grep -w) */
#define REGEX_LINE       16     /* a match has to be the whole text (grep -x) */
#define REGEX_GROUPS     32     /* regex_groups will be used: keep what the groups matched */

/**
 * Compiles the patterns into one expression that matches what any of them matches.
 * Returns NULL after writing the reason into error (GNU wording, "Unmatched ( or \(").
 */
Regex* regex_compile(const char* const* patterns, size_t count, int flags, char* error, size_t errorSize);

void regex_free(Regex* regex);

typedef struct {
    uint32_t* chars;            /* code points; 0x110000 + byte for a byte that is not UTF-8 */
    size_t*   offset;           /* offset[i] is the byte offset of chars[i]; offset[length] is the byte length */
    size_t    length;
    size_t    capacity;
} RegexInput;

/** Decodes bytes[0..length) into input (which is reused). Returns 0, or -1 when out of memory. */
int regex_input_set(RegexInput* input, const unsigned char* bytes, size_t length);

void regex_input_free(RegexInput* input);

/**
 * Looks for the leftmost, longest match that starts at or after character index `from`.
 * Returns 1 and the character indexes [*start, *end) of the match, or 0 when there is none.
 * An empty match has *start == *end.
 */
int regex_search(const Regex* regex, const RegexInput* input, size_t from, size_t* start, size_t* end);

/** The number of groups (parenthesized subexpressions) of the expression, which is the n of \n. */
size_t regex_group_count(const Regex* regex);

/**
 * The groups of a match that regex_search found: spans[2n] and spans[2n + 1] are the start and the end
 * (in characters of the input) of group n, group 0 being the whole match; a group that took no part is
 * (size_t)-1. spans has 2 * (regex_group_count + 1) entries. Among the ways to match the same text the
 * first one in the order of a backtracking matcher (greedy, leftmost alternative) is taken.
 * Returns 0 when the groups could not be found (the whole match is set anyway).
 */
int regex_groups(const Regex* regex, const RegexInput* input, size_t start, size_t end, size_t* spans);

#endif //REGEX_H
