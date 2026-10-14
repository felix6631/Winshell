#include "variables.h"
#include "vec.h"
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
    char* copy;

    if (VEC_GROW_BY_COUNT(entries, entryCount) != 0)
        return NULL;
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

/* ---- the state of the variables, for what runs in a subshell ------------------------------ */

struct VarSnapshot {
    char*  environment;     // the block of "NAME=value" strings of the process, ended by an empty one
    size_t environmentSize;
    Entry* entries;
    size_t entryCount;
};

static size_t block_size(const char* block) {
    const char* p = block;
    while (*p != '\0')
        p += strlen(p) + 1;
    return (size_t)(p - block) + 1;
}

/** The value of NAME in an environment block of the process, or NULL; case does not matter on Windows. */
static const char* block_get(const char* block, const char* name) {
    size_t length = strlen(name);
    const char* p;

    for (p = block; *p != '\0'; p += strlen(p) + 1)
        if (_strnicmp(p, name, length) == 0 && p[length] == '=')
            return p + length + 1;
    return NULL;
}

VarSnapshot* var_save(void) {
    VarSnapshot* s = calloc(1, sizeof *s);
    char* current;
    size_t i;

    if (s == NULL)
        return NULL;
    current = GetEnvironmentStringsA();
    if (current == NULL) {
        free(s);
        return NULL;
    }
    s->environmentSize = block_size(current);
    s->environment = malloc(s->environmentSize);
    if (s->environment != NULL)
        memcpy(s->environment, current, s->environmentSize);
    FreeEnvironmentStringsA(current);

    // the array is grown by VEC_GROW_BY_COUNT, which takes its capacity from the count (4, then powers of two)
    if (entryCount > 0) {
        size_t capacity = 4;
        while (capacity < entryCount)
            capacity *= 2;
        s->entries = calloc(capacity, sizeof *s->entries);
    }
    if (s->environment == NULL || (entryCount > 0 && s->entries == NULL)) {
        free(s->environment);
        free(s->entries);
        free(s);
        return NULL;
    }
    for (i = 0; i < entryCount; i++) {
        s->entries[i].name = _strdup(entries[i].name);
        s->entries[i].value = entries[i].value != NULL ? _strdup(entries[i].value) : NULL;
        s->entries[i].exported = entries[i].exported;
    }
    s->entryCount = entryCount;
    return s;
}

void var_restore(VarSnapshot* s) {
    char* current;
    const char* p;
    size_t i;

    if (s == NULL)
        return;

    // what the subshell added to the environment goes, what it changed is put back
    current = GetEnvironmentStringsA();
    if (current != NULL) {
        for (p = current; *p != '\0'; p += strlen(p) + 1) {
            const char* equals = strchr(p + (*p == '=' ? 1 : 0), '=');
            char name[512];
            size_t length;

            if (*p == '=' || equals == NULL)
                continue;                   // the per-drive directories of Windows are not variables
            length = (size_t)(equals - p);
            if (length >= sizeof name)
                continue;
            memcpy(name, p, length);
            name[length] = '\0';
            if (block_get(s->environment, name) == NULL) {
                _putenv_s(name, "");
                SetEnvironmentVariableA(name, NULL);
            }
        }
        FreeEnvironmentStringsA(current);
    }
    for (p = s->environment; *p != '\0'; p += strlen(p) + 1) {
        const char* equals = strchr(p + (*p == '=' ? 1 : 0), '=');
        char name[512];
        size_t length;
        char* now;

        if (*p == '=' || equals == NULL)
            continue;
        length = (size_t)(equals - p);
        if (length >= sizeof name)
            continue;
        memcpy(name, p, length);
        name[length] = '\0';
        now = getenv(name);
        if (now == NULL || strcmp(now, equals + 1) != 0 || equals[1] == '\0')
            env_put(name, equals + 1);
    }

    for (i = 0; i < entryCount; i++) {
        free(entries[i].name);
        free(entries[i].value);
    }
    free(entries);
    entries = s->entries;
    entryCount = s->entryCount;

    free(s->environment);
    free(s);
}
