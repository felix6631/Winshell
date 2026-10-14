#ifndef CWD_H
#define CWD_H

#include <stddef.h>

/**
 * The current directory of the shell, and the variables PWD and OLDPWD that describe it.
 *
 * Windows has one current directory per drive and no root, so the virtual root "/" has no directory
 * of its own (the process waits on the system drive then). The shell keeps the virtual directory in
 * the exported variable PWD, which the programs it starts find through vpath_getcwd; OLDPWD is the
 * directory before the last change. Every change of them goes through this module.
 */

typedef enum {
    CWD_OK,
    CWD_NOT_FOUND,
    CWD_NOT_DIRECTORY,
    CWD_DENIED
} CwdResult;

/** Makes PWD describe the directory the shell starts in. */
void cwd_init(void);

/** The current directory as an absolute virtual path ("/" when it cannot be told). */
void cwd_get(char* out, size_t size);

/** OLDPWD: the directory before the last change, or NULL. */
const char* cwd_previous(void);

/**
 * Changes the directory to target (a path of the virtual file system, relative to the current
 * directory). On success PWD and OLDPWD are updated and the new absolute virtual path is in absolute.
 */
CwdResult cwd_change(const char* target, char* absolute, size_t size);

#endif //CWD_H
