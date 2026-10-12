#include "program.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"

static int ends_with_exe(const char* name) {
    size_t length = strlen(name);
    return length >= 4 && _stricmp(name + length - 4, ".exe") == 0;
}

static int exists(const char* path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

int program_find(const char* program, char* path, size_t size) {
    int written;

    if (strchr(program, '/') != NULL || strchr(program, '\\') != NULL) {
        char native[VPATH_MAX];

        if (vpath_to_native(program, native, sizeof native) != 0)
            return -1;
        written = snprintf(path, size, "%s", native);
        if (written < 0 || (size_t)written >= size)
            return -1;
        if (!exists(path) && !ends_with_exe(native)) {
            written = snprintf(path, size, "%s.exe", native);
            if (written < 0 || (size_t)written >= size)
                return -1;
        }
    } else {
        char directory[MAX_PATH];
        DWORD length = GetModuleFileNameA(NULL, directory, MAX_PATH);
        char* separator;

        if (length == 0 || length >= MAX_PATH)
            return -1;
        separator = strrchr(directory, '\\');
        if (separator == NULL)
            return -1;
        *separator = '\0';

        written = snprintf(path, size, ends_with_exe(program) ? "%s\\%s" : "%s\\%s.exe", directory, program);
        if (written < 0 || (size_t)written >= size)
            return -1;
    }
    return exists(path) ? 0 : -1;
}
