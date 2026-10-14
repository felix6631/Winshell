#ifndef SED_H
#define SED_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "regex.h"
#include "vec.h"

/*
 * sed: the script (script.c) and its execution (exec.c) meet in these types.
 */

typedef struct {
    unsigned char* data;
    size_t         length, capacity;
} Buf;

/** Appends bytes. Returns 0, or -1 when out of memory. */
static __inline int buf_add(Buf* b, const void* data, size_t length) {
    if (length == 0)
        return 0;
    if (VEC_RESERVE(b->data, b->capacity, b->length + length + 1) != 0)
        return -1;
    memcpy(b->data + b->length, data, length);
    b->length += length;
    return 0;
}

static __inline int buf_add_byte(Buf* b, int c) {
    unsigned char byte = (unsigned char)c;
    return buf_add(b, &byte, 1);
}

/** An output: standard output, a file of a w command, or the temporary file of -i. */
typedef struct Output {
    char*          name;
    FILE*          file;
    int            missingNewline;      // the last line written had no line ending: one is due before more
    struct Output* next;
} Output;

typedef enum {
    ADDR_NONE,
    ADDR_LINE,          // N
    ADDR_LAST,          // $
    ADDR_REGEX,         // /re/ (regex NULL: the last regular expression used)
    ADDR_STEP,          // FIRST~STEP
    ADDR_PLUS,          // addr1,+N
    ADDR_MULTIPLE,      // addr1,~N
    ADDR_ZERO           // 0 in 0,/re/
} AddrKind;

typedef struct {
    AddrKind kind;
    long     number;
    long     step;
    Regex*   regex;
} Address;

typedef enum { PART_TEXT, PART_GROUP, PART_CASE } PartKind;

/** A piece of the right hand side of s: text, a group (0 is &), or a case change (U L E u l). */
typedef struct {
    PartKind       kind;
    unsigned char* text;
    size_t         length;
    int            number;
} Part;

typedef struct {
    Regex*  regex;              // NULL: the last regular expression used
    Part*   parts;
    size_t  partCount;
    int     global;
    int     print;
    long    number;             // replace from the nth match on
    int     needGroups;
    Output* out;                // flag w
} Substitution;

typedef struct {
    Address        first, second;
    int            negate;
    int            name;
    int            rangeActive;
    long           rangeEnd;
    unsigned char* text;        // a i c: the text; r R w W: the file name; : b t T: the label
    size_t         length;
    long           number;      // q Q: the exit status; l: the width (-1: none given)
    size_t         target;      // b t T: the command to go on with; {: the matching }
    Substitution*  sub;
    uint32_t*      yFrom;
    uint32_t*      yTo;
    size_t         yCount;
    Output*        out;         // w W
    struct Reader* reader;      // R
} Command;

typedef struct {
    Command* commands;
    size_t   count;
    int      quiet;             // the first line of the script is #n
    Output*  outputs;           // the files of the w commands, opened when the script was parsed
} Script;

/** A piece of the script: an -e option or the content of an -f file. */
typedef struct {
    const char* text;
    size_t      length;
    const char* file;           // NULL for -e
    int         number;         // of the -e
} ScriptChunk;

typedef struct {
    int quiet;                  // -n
    int separate;               // -s (and -i)
    int inPlace;                // -i
    const char* suffix;         // of -i: for the backup, or NULL
    int terminator;             // '\n', or 0 with -z
    long lineWrap;              // of the l command
    int unbuffered;
    int sandbox;
    int extended;
} SedConfig;

/** Parses the chunks. Returns 0, or the exit status (1, or 4 for what GNU sed calls a panic) with a message in error. */
int sed_parse(const ScriptChunk* chunks, size_t chunkCount, const SedConfig* config, Script* script,
              char* error, size_t errorSize);

/**
 * The output for a file name of a w command (opened, and emptied, at once; the script keeps them): /dev/stdout and
 * /dev/stderr are the standard streams. Returns NULL after a message when it cannot be opened.
 */
Output* sed_output_open(Script* script, const char* name);

/** Runs the script over the files ("-" is standard input). Returns the exit status. */
int sed_run(Script* script, const SedConfig* config, const char* const* files, size_t fileCount);

#endif //SED_H
