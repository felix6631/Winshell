#include "program.h"
#include <stdio.h>
#include <stdlib.h>
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

/** The directories of PATH, one after the other: the program is looked for in each, with the extensions Windows runs. */
static int find_in_path(const char* program, char* path, size_t size) {
    static const char* const extensions[] = { "", ".exe", ".com", ".cmd", ".bat", NULL };
    const char* list = getenv("PATH");
    int hasExtension = strrchr(program, '.') != NULL;

    if (list == NULL)
        return -1;
    while (*list != '\0') {
        const char* end = strchr(list, ';');
        size_t length = end != NULL ? (size_t)(end - list) : strlen(list);
        int e;

        if (length > 0 && length < MAX_PATH) {
            char directory[MAX_PATH];

            memcpy(directory, list, length);
            directory[length] = '\0';
            if (directory[0] == '"' && directory[length - 1] == '"') {
                memmove(directory, directory + 1, length - 2);
                directory[length - 2] = '\0';
            }
            for (e = 0; extensions[e] != NULL; e++) {
                int written;

                if (e == 0 && !hasExtension)
                    continue;                       // a name without an extension is not a program by itself
                written = snprintf(path, size, "%s\\%s%s", directory, program, extensions[e]);
                if (written > 0 && (size_t)written < size) {
                    DWORD attributes = GetFileAttributesA(path);

                    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                        return 0;
                }
            }
        }
        if (end == NULL)
            break;
        list = end + 1;
    }
    return -1;
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
    if (exists(path))
        return 0;
    if (strchr(program, '/') != NULL || strchr(program, '\\') != NULL)
        return -1;
    return find_in_path(program, path, size);
}
