#ifndef EXECUTER_H
#define EXECUTER_H

#include <stdarg.h>

/**
 * execute recieves program name and execute it. 
 * the va arg is the param of the program.
 */
int execute(const char* program, ...);

#endif //EXECUTER_H