#ifndef UTF8_H
#define UTF8_H

#include <stddef.h>
#include <stdint.h>

/*
 * UTF-8 text, as the tools see it: the programs run with the UTF-8 code page (see utf8.manifest),
 * so names, arguments and data are UTF-8. A character is a whole sequence.
 */

/** Code points from this value on stand for the bytes that are not UTF-8 (UTF8_INVALID_BASE + byte). */
#define UTF8_INVALID_BASE 0x110000u

/**
 * Decodes the character at s, of which `left` bytes are there. Sets *length (at least 1). An invalid or
 * truncated sequence gives UTF8_INVALID_BASE + its first byte and length 1.
 */
uint32_t utf8_decode(const unsigned char* s, size_t left, size_t* length);

/** Writes the character and returns the number of bytes (1 to 4); out has room for 4. */
size_t utf8_encode(uint32_t c, char* out);

/**
 * Case mapping of ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic, by their layout (the C library
 * knows nothing beyond ASCII in the "C" locale). Other characters are returned as they are.
 */
uint32_t utf8_fold(uint32_t c);
uint32_t utf8_upper(uint32_t c);

#endif //UTF8_H
