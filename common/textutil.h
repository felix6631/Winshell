#ifndef TEXTUTIL_H
#define TEXTUTIL_H

#include <stdio.h>

/*
 * Helpers of the commands that read text: head, tail and wc.
 *
 * A line ends at a line feed. A carriage return in front of it belongs to
 * the line and is passed on as it is, so files with LF and with CR LF line
 * endings are handled alike, and nothing is changed in them.
 */

/** A number such as the argument of -n and -c: 10, +5, -5, 10K, 2M, 3kB. */
typedef struct {
    long long value;    // the magnitude
    int       plus;     // written with a leading +
    int       minus;    // written with a leading -
} TextCount;

/**
 * Parses [+-]digits[suffix]. Suffixes: b (512), k K (1024), m M, g G, t T
 * (powers of 1024), and kB KB MB GB (powers of 1000).
 * Returns 0, or -1 when text is not a number.
 */
int text_parse_count(const char* text, TextCount* out);

/**
 * Opens a file for reading in binary mode; "-" is standard input.
 * The path is one of the virtual file system.
 * With plainMessage the error reads "program: path: reason" as wc and cat
 * print it; otherwise "program: cannot open 'path' for reading: reason".
 * Returns 0 and the stream, or -1 after printing the error.
 * A stream that is not standard input has to be closed with fclose.
 */
int text_open(const char* program, const char* path, int plainMessage, FILE** out);

/** Puts standard input and standard output into binary mode. */
void text_binary_streams(void);

/**
 * Reads what is there, up to size bytes, and does not wait for more: from a pipe or a console
 * that is what the writer has written so far, which is what a filter that passes its input on
 * has to work with. fread would wait until the buffer is full, and a program such as
 * "tail -f log | head -n 2" would never end. Returns 0 at the end of the input, or -1 on an error.
 */
long text_read_some(FILE* in, void* buffer, size_t size);

/** Reads a stream completely. The result has to be freed; NULL when out of memory. */
unsigned char* text_slurp(FILE* in, size_t* length);

/**
 * One line of a text. The content is what stands before the line ending; a
 * CR in front of the line feed is part of the line ending, not of the
 * content, so "a\r\n" and "a\n" have the same content "a".
 */
typedef struct {
    const unsigned char* content;
    size_t               length;        // of the content
    int                  ending;        // 0 none (last line), 1 LF, 2 CR LF
} TextLine;

/** Where the line that starts at text + *offset ends; fills line and moves *offset. Returns 0 at the end. */
int text_next_line(const unsigned char* text, size_t length, size_t* offset, TextLine* line);

/** Writes the content and then the ending of the line (a missing one is written as LF). */
int text_write_line(const TextLine* line, FILE* out);

#endif //TEXTUTIL_H
