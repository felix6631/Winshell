#include "variables.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct {
    char* name;
    char* value;        // NULL for a name that was only exported
    int   exported;     // the next assignment goes to the environment
} Entry;

static Entry* entries = NULL;
static size_t entryCount = 0;

static Entry* find_entry(const char* name) {
    size_t i;
    for (i = 0; i < entryCount; i++)
        if (strcmp(entries[i].name, name) == 0)
            return &entries[i];
    return NULL;
}

static void remove_entry(Entry* entry) {
    free(entry->name);
    free(entry->value);
    *entry = entries[--entryCount];
}

static int env_has(const char* name) {
    // an empty value is in the environment of Windows, but not found by getenv
    return GetEnvironmentVariableA(name, NULL, 0) != 0;
}

static void env_put(const char* name, const char* value) {
    if (value[0] == '\0') {
        // _putenv_s with "" removes the variable; Windows can hold it empty
        _putenv_s(name, "");
        SetEnvironmentVariableA(name, "");
    } else {
        _putenv_s(name, value);
    }
}

static Entry* new_entry(const char* name) {
    Entry* grown = realloc(entries, (entryCount + 1) * sizeof *grown);
    char* copy;

    if (grown == NULL)
        return NULL;
    entries = grown;
    copy = _strdup(name);
    if (copy == NULL)
        return NULL;
    entries[entryCount].name = copy;
    entries[entryCount].value = NULL;
    entries[entryCount].exported = 0;
    return &entries[entryCount++];
}

const char* var_get(const char* name) {
    const char* value = getenv(name);
    Entry* entry;

    if (value != NULL)
        return value;
    entry = find_entry(name);
    return entry != NULL ? entry->value : NULL;
}

int var_set(const char* name, const char* value) {
    Entry* entry = find_entry(name);

    if (env_has(name) || (entry != NULL && entry->exported)) {
        env_put(name, value);
        if (entry != NULL)
            remove_entry(entry);
        return 0;
    }

    {
        char* copy = _strdup(value);
        if (copy == NULL)
            return -1;
        if (entry == NULL) {
            entry = new_entry(name);
            if (entry == NULL) {
                free(copy);
                return -1;
            }
        }
        free(entry->value);
        entry->value = copy;
    }
    return 0;
}

int var_export(const char* name, const char* value) {
    Entry* entry = find_entry(name);

    if (value != NULL) {
        env_put(name, value);
        if (entry != NULL)
            remove_entry(entry);
        return 0;
    }

    if (env_has(name))
        return 0;

    if (entry == NULL) {
        entry = new_entry(name);
        if (entry == NULL)
            return -1;
    }
    if (entry->value != NULL) {
        env_put(name, entry->value);
        remove_entry(entry);
    } else {
        entry->exported = 1;
    }
    return 0;
}

void var_unset(const char* name) {
    Entry* entry = find_entry(name);

    if (entry != NULL)
        remove_entry(entry);
    _putenv_s(name, "");        // an empty value removes the variable
    SetEnvironmentVariableA(name, NULL);
}
