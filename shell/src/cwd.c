#include "cwd.h"
#include "variables.h"
#include "vpath.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

void cwd_init(void) {
    char cwd[VPATH_MAX];

    if (vpath_getcwd(cwd, sizeof cwd) == 0)
        var_export("PWD", cwd);
}

void cwd_get(char* out, size_t size) {
    if (vpath_getcwd(out, size) != 0)
        snprintf(out, size, "/");
}

const char* cwd_previous(void) {
    const char* value = var_get("OLDPWD");
    return value != NULL && value[0] != '\0' ? value : NULL;
}

CwdResult cwd_change(const char* target, char* absolute, size_t size) {
    char current[VPATH_MAX], native[VPATH_MAX];
    int kind;

    cwd_get(current, sizeof current);
    if (vpath_resolve(current, target, absolute, size) != 0)
        return CWD_NOT_FOUND;

    kind = vpath_absolute_to_native(absolute, native, sizeof native);
    if (kind == VPATH_ROOT) {
        // the root has no directory of its own; the process waits on the system drive
        if (vpath_root_native(native, sizeof native) != 0)
            return CWD_NOT_FOUND;
    } else if (kind != 0) {
        return CWD_NOT_FOUND;
    } else {
        DWORD attributes = GetFileAttributesA(native);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return CWD_NOT_FOUND;
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return CWD_NOT_DIRECTORY;
    }

    if (!SetCurrentDirectoryA(native))
        return CWD_DENIED;

    var_export("OLDPWD", current);
    var_export("PWD", absolute);
    return CWD_OK;
}
