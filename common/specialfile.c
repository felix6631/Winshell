#include <string.h>
#include <windows.h>
#include "specialfile.h"
#include "vpath.h"

const char* special_file_reason(const char* name) {
    char native[VPATH_MAX];
    char parent[VPATH_MAX];
    char* slash;
    DWORD attributes;
    int kind;

    if (name[0] == '\0')
        return "No such file or directory";
    kind = vpath_to_native(name, native, sizeof native);
    if (kind == VPATH_ROOT)
        return "File exists";
    if (kind != 0)
        return "No such file or directory";
    if (GetFileAttributesA(native) != INVALID_FILE_ATTRIBUTES)
        return "File exists";
    strcpy(parent, native);
    slash = strrchr(parent, '\\');
    if (slash != NULL && slash > parent) {
        if (slash == parent + 2 && parent[1] == ':')
            slash++;                                    /* C:\ keeps its backslash */
        *slash = '\0';
        attributes = GetFileAttributesA(parent);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return "No such file or directory";
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return "Not a directory";
    }
    return "Operation not supported";
}
