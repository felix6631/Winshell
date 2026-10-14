#ifndef ARITH_H
#define ARITH_H

#include <stddef.h>

/**
 * The value of an arithmetic expression, as in $(( ... )): 64-bit integers, the operators of C, variables
 * by name (which can be assigned to). An empty expression is 0.
 * Returns 0, or -1 with a message in error (a syntax error, division by 0).
 */
int arith_eval(const char* text, long long* result, char* error, size_t errorSize);

#endif //ARITH_H
