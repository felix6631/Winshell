#ifndef VARIABLES_H
#define VARIABLES_H

/**
 * The variables of the shell. As in UNIX there are two kinds: the ones that
 * only the shell knows (NAME=value), and the exported ones, which are in the
 * environment and so in the programs the shell starts (export NAME=value).
 * A variable that is in the environment stays there when it is assigned to.
 */

/** The value of a variable of the shell or of the environment; NULL when it is not set. */
const char* var_get(const char* name);

/**
 * NAME=value: changes the value, and creates the variable as one of the shell
 * only. A variable that is exported stays exported. Returns 0, or -1 (out of memory).
 */
int var_set(const char* name, const char* value);

/**
 * export NAME[=value]: puts the variable in the environment, with the value
 * it has, or with value when that is not NULL. An export of a name that is not
 * set marks it, and the next assignment goes to the environment.
 * Returns 0, or -1 (out of memory).
 */
int var_export(const char* name, const char* value);

/** unset NAME: removes the variable, whichever kind it is. */
void var_unset(const char* name);

#endif //VARIABLES_H
