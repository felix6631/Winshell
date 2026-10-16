#ifndef VARIABLES_H
#define VARIABLES_H

#include <stddef.h>

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

/** Calls fn for every variable of the shell that is not exported (the parameters too), in no order. */
void var_each_local(void (*fn)(const char* name, const char* value, void* context), void* context);

/**
 * Arrays (indexed, sparse). An array is a variable of the shell only (it is not exported); a scalar variable that
 * is not exported becomes the element 0 of an array when it is made one, and $NAME of an array is its element 0.
 * A negative index counts from the end (-1 is the last element). unset NAME removes the whole array.
 *
 * var_array_assign: NAME=(values): replaces the elements by 0..count-1. var_array_append: NAME+=(values), after
 * the highest index. var_array_set: NAME[index]=value. All return 0, or -1 (out of memory, or an exported variable).
 * var_array_get: the element, or NULL when it is not set (a scalar is an array of one element).
 * var_array_list: copies of the values and their indices in order (free with var_array_free_list).
 */
int var_is_array(const char* name);
int var_array_assign(const char* name, char* const* values, size_t count);
int var_array_append(const char* name, char* const* values, size_t count);
int var_array_set(const char* name, long index, const char* value);
const char* var_array_get(const char* name, long index);
int var_array_list(const char* name, char*** values, long** indices, size_t* count);
void var_array_free_list(char** values, long* indices, size_t count);
void var_array_unset(const char* name, long index);

/** Calls fn for every element of every array (an empty array once, with index -1 and no value). */
void var_each_array(void (*fn)(const char* name, long index, const char* value, void* context), void* context);

/** unset NAME: removes the variable, whichever kind it is. */
void var_unset(const char* name);

/**
 * The state of the variables at one moment: the environment and the variables of the shell. What runs in
 * a subshell (a command substitution) changes variables that the shell around it has to see unchanged:
 * var_save before, var_restore after (it also frees the snapshot). var_save returns NULL when out of memory.
 */
typedef struct VarSnapshot VarSnapshot;
VarSnapshot* var_save(void);
void var_restore(VarSnapshot* snapshot);

#endif //VARIABLES_H
