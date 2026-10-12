#ifndef CMDLINE_H
#define CMDLINE_H

/**
 * Joins argv (NULL-terminated) into one command line for CreateProcess,
 * every argument quoted by the rules of CommandLineToArgvW, so the program
 * gets the very same arguments back. The result has to be freed; NULL when
 * out of memory.
 */
char* cmdline_build(char* const argv[]);

#endif //CMDLINE_H
