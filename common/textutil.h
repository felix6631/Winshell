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

#endif //TEXTUTIL_H
