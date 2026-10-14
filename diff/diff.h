#ifndef DIFF_H
#define DIFF_H

#include <stddef.h>
#include <stdio.h>
#include <windows.h>

/*
 * diff: the types that main.c (options, files and directories), lcs.c (what differs) and
 * output.c (the three formats) share.
 */

typedef enum { FORMAT_NORMAL, FORMAT_UNIFIED, FORMAT_CONTEXT } Format;

typedef struct {
    Format format;
    long   context;             // lines of context for -u and -c
    int    ignoreCase;          // -i
    int    ignoreAllSpace;      // -w
    int    ignoreSpaceChange;   // -b
    int    ignoreBlankLines;    // -B
    int    stripTrailingCr;
    int    treatAsText;         // -a
} Settings;

typedef struct {
    size_t start, length;       // of the content of a line, without its line feed
} Span;

/** A file read completely and cut into lines. */
typedef struct {
    unsigned char* text;
    size_t         length;
    Span*          lines;
    size_t         count;
    int*           ids;         // lines that compare equal have the same id
    char*          changed;     // set by diff_lines: the line is not in the common part
    int            noNewline;   // the last line has no line feed
    int            binary;
} Text;

/** Lines [a0, a1) of the first file are replaced by lines [b0, b1) of the second. */
typedef struct {
    size_t a0, a1, b0, b1;
} Hunk;

/** Reads a stream and splits it into lines (a CR before the line feed is content unless stripped). */
int text_load(Text* t, FILE* in, int stripCr);
void text_free(Text* t);

/** Gives the lines their ids under the settings, both files with one numbering. Returns 0, or -1 when out of memory. */
int text_prepare(Text* a, Text* b, const Settings* settings);

/**
 * Marks the lines of both files that are not part of the common sequence. `context` is the number of
 * lines of the ends the files share that still take part (GNU diff: 0 for the normal format).
 * Returns 0, or -1 when out of memory.
 */
int diff_lines(Text* a, Text* b, long context);

/** The changes as hunks (all of them are reported); the caller frees *out. */
int diff_hunks(const Text* a, const Text* b, const Settings* settings, Hunk** out, size_t* count);

/** What is printed above the lines of a file in the unified and context formats. */
typedef struct {
    const char* label;
    FILETIME    modified;
    int         showTime;       // not for a label given with --label
} Heading;

/** Writes the hunks in the format of the settings. */
void diff_output(const Settings* settings, const Text* a, const Text* b, const Heading* first, const Heading* second,
                 const Hunk* hunks, size_t count);

#endif //DIFF_H
