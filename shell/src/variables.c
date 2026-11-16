#include "variables.h"
#include "vec.h"
#include "vpath.h"
#include "arith.h"
#include <stdio.h>
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


/* ---- arrays ------------------------------------------------------------------------------ */

typedef struct {
    long  index;
    char* value;
} Element;

typedef struct {
    char*    name;
    Element* items;         // in the order of the indices
    size_t   count;
} ArrayVar;

static ArrayVar* arrays;
static size_t arrayCount;

static ArrayVar* find_array(const char* name) {
    size_t i;

    for (i = 0; i < arrayCount; i++)
        if (strcmp(arrays[i].name, name) == 0)
            return &arrays[i];
    return NULL;
}

static void free_items(Element* items, size_t count) {
    size_t i;

    for (i = 0; i < count; i++)
        free(items[i].value);
    free(items);
}

static void remove_array(ArrayVar* array) {
    free(array->name);
    free_items(array->items, array->count);
    *array = arrays[--arrayCount];
}

int var_is_array(const char* name) {
    return find_array(name) != NULL;
}

/** The array of that name; a scalar of the shell becomes its element 0. NULL (and a message) when it cannot be one. */
static ArrayVar* array_for_write(const char* name) {
    ArrayVar* array = find_array(name);
    Entry* entry;
    ArrayVar* grown;
    char* copy;

    if (array != NULL)
        return array;
    if (env_has(name)) {
        fprintf(stderr, "Error: %s: an exported variable cannot be an array\n", name);
        return NULL;
    }
    grown = realloc(arrays, (arrayCount + 1) * sizeof *grown);
    if (grown == NULL)
        return NULL;
    arrays = grown;
    array = &arrays[arrayCount];
    array->name = _strdup(name);
    array->items = NULL;
    array->count = 0;
    if (array->name == NULL)
        return NULL;
    arrayCount++;

    entry = find_entry(name);
    if (entry != NULL) {
        if (entry->value != NULL) {
            copy = _strdup(entry->value);
            array->items = malloc(sizeof *array->items);
            if (copy == NULL || array->items == NULL) {
                free(copy);
                remove_array(array);
                return NULL;
            }
            array->items[0].index = 0;
            array->items[0].value = copy;
            array->count = 1;
        }
        remove_entry(entry);
    }
    return array;
}

static int put_element(ArrayVar* array, long index, const char* value) {
    size_t i;
    char* copy = _strdup(value);
    Element* grown;

    if (copy == NULL)
        return -1;
    for (i = 0; i < array->count && array->items[i].index < index; i++)
        ;
    if (i < array->count && array->items[i].index == index) {
        free(array->items[i].value);
        array->items[i].value = copy;
        return 0;
    }
    grown = realloc(array->items, (array->count + 1) * sizeof *grown);
    if (grown == NULL) {
        free(copy);
        return -1;
    }
    array->items = grown;
    memmove(array->items + i + 1, array->items + i, (array->count - i) * sizeof *array->items);
    array->items[i].index = index;
    array->items[i].value = copy;
    array->count++;
    return 0;
}

int var_array_set(const char* name, long index, const char* value) {
    ArrayVar* array = array_for_write(name);

    if (array == NULL)
        return -1;
    if (index < 0) {
        long last = array->count > 0 ? array->items[array->count - 1].index : -1;

        index += last + 1;
        if (index < 0) {
            fprintf(stderr, "Error: %s: bad array subscript\n", name);
            return -1;
        }
    }
    return put_element(array, index, value);
}

int var_array_assign(const char* name, char* const* values, size_t count) {
    ArrayVar* array = array_for_write(name);
    size_t i;

    if (array == NULL)
        return -1;
    free_items(array->items, array->count);
    array->items = NULL;
    array->count = 0;
    for (i = 0; i < count; i++)
        if (put_element(array, (long)i, values[i]) != 0)
            return -1;
    return 0;
}

int var_array_append(const char* name, char* const* values, size_t count) {
    ArrayVar* array = array_for_write(name);
    long next;
    size_t i;

    if (array == NULL)
        return -1;
    next = array->count > 0 ? array->items[array->count - 1].index + 1 : 0;
    for (i = 0; i < count; i++)
        if (put_element(array, next + (long)i, values[i]) != 0)
            return -1;
    return 0;
}

const char* var_array_get(const char* name, long index) {
    ArrayVar* array = find_array(name);
    size_t i;

    if (array == NULL) {
        // a scalar is an array of one element
        const char* value = var_get(name);

        return value != NULL && (index == 0 || index == -1) ? value : NULL;
    }
    if (index < 0)
        index += array->count > 0 ? array->items[array->count - 1].index + 1 : 0;
    for (i = 0; i < array->count; i++)
        if (array->items[i].index == index)
            return array->items[i].value;
    return NULL;
}

int var_array_list(const char* name, char*** values, long** indices, size_t* count) {
    ArrayVar* array = find_array(name);
    size_t i;

    *values = NULL;
    *indices = NULL;
    *count = 0;
    if (array == NULL) {
        const char* value = var_get(name);

        if (value == NULL)
            return 0;
        *values = calloc(2, sizeof **values);
        *indices = calloc(1, sizeof **indices);
        if (*values == NULL || *indices == NULL || ((*values)[0] = _strdup(value)) == NULL) {
            free(*values);
            free(*indices);
            *values = NULL;
            *indices = NULL;
            return -1;
        }
        (*indices)[0] = 0;
        *count = 1;
        return 0;
    }
    *values = calloc(array->count + 1, sizeof **values);
    *indices = calloc(array->count + 1, sizeof **indices);
    if (*values == NULL || *indices == NULL) {
        free(*values);
        free(*indices);
        *values = NULL;
        *indices = NULL;
        return -1;
    }
    for (i = 0; i < array->count; i++) {
        (*values)[i] = _strdup(array->items[i].value);
        (*indices)[i] = array->items[i].index;
        if ((*values)[i] == NULL) {
            var_array_free_list(*values, *indices, i);
            *values = NULL;
            *indices = NULL;
            return -1;
        }
    }
    *count = array->count;
    return 0;
}

void var_array_free_list(char** values, long* indices, size_t count) {
    size_t i;

    for (i = 0; values != NULL && i < count; i++)
        free(values[i]);
    free(values);
    free(indices);
}

void var_array_unset(const char* name, long index) {
    ArrayVar* array = find_array(name);
    size_t i;

    if (array == NULL) {
        if (index == 0 || index == -1)
            var_unset(name);
        return;
    }
    if (index < 0)
        index += array->count > 0 ? array->items[array->count - 1].index + 1 : 0;
    for (i = 0; i < array->count; i++) {
        if (array->items[i].index == index) {
            free(array->items[i].value);
            memmove(array->items + i, array->items + i + 1, (array->count - i - 1) * sizeof *array->items);
            array->count--;
            if (array->count == 0)
                remove_array(array);
            return;
        }
    }
}

void var_each_array(void (*fn)(const char* name, long index, const char* value, void* context), void* context) {
    size_t a, i;

    for (a = 0; a < arrayCount; a++) {
        if (arrays[a].count == 0)
            fn(arrays[a].name, -1, NULL, context);
        for (i = 0; i < arrays[a].count; i++)
            fn(arrays[a].name, arrays[a].items[i].index, arrays[a].items[i].value, context);
    }
}

/** A copy of all the arrays, for the snapshot. */
static ArrayVar* copy_arrays(size_t* count) {
    ArrayVar* copy = calloc(arrayCount + 1, sizeof *copy);
    size_t a, i;

    *count = arrayCount;
    if (copy == NULL)
        return NULL;
    for (a = 0; a < arrayCount; a++) {
        copy[a].name = _strdup(arrays[a].name);
        copy[a].items = calloc(arrays[a].count + 1, sizeof *copy[a].items);
        copy[a].count = arrays[a].count;
        if (copy[a].name == NULL || copy[a].items == NULL)
            return copy;                    // out of memory: what was copied is what is kept
        for (i = 0; i < arrays[a].count; i++) {
            copy[a].items[i].index = arrays[a].items[i].index;
            copy[a].items[i].value = _strdup(arrays[a].items[i].value);
        }
    }
    return copy;
}

static void free_arrays(ArrayVar* list, size_t count) {
    size_t a;

    for (a = 0; a < count; a++) {
        free(list[a].name);
        if (list[a].items != NULL)
            free_items(list[a].items, list[a].count);
    }
    free(list);
}

const char* var_get(const char* name) {
    const char* value;
    Entry* entry;

    if (find_array(name) != NULL)
        return var_array_get(name, 0);      // $a is ${a[0]}
    value = getenv(name);
    if (value != NULL)
        return value;
    entry = find_entry(name);
    return entry != NULL ? entry->value : NULL;
}

/*
 * PATH as a script of UNIX writes it ("$PWD/bin:/usr/local/bin:$PATH", the directories as /c/dir and a colon between them) is a
 * list that Windows cannot read: it takes ; between directories and C:\dir. Like MSYS the list is turned into the Windows form
 * when it is assigned. Returns a malloc'ed copy, or NULL when the value is a Windows list already (or on no memory).
 */
static int is_separator_at(const char* value, size_t i, size_t start) {
    if (value[i] == '\0' || value[i] == ';')
        return 1;
    // a colon is the one after a drive letter when it is the second character of the directory and a slash follows
    return value[i] == ':' && !(i == start + 1 && (value[i + 1] == '\\' || value[i + 1] == '/'));
}

static char* windows_path_list(const char* value) {
    size_t length = strlen(value), i, start = 0, used = 0;
    char* out;
    int convert = value[0] == '/';

    for (i = 0; i <= length; i++) {
        if (is_separator_at(value, i, start)) {
            if (value[i] == ':')
                convert = 1;
            start = i + 1;
            if (value[start] == '/')
                convert = 1;
        }
    }
    if (!convert)
        return NULL;
    out = malloc(length * 2 + 8);
    if (out == NULL)
        return NULL;
    start = 0;
    for (i = 0; i <= length; i++) {
        if (!is_separator_at(value, i, start))
            continue;
        if (i > start && i - start < VPATH_MAX) {
            char piece[VPATH_MAX], native[VPATH_MAX];

            memcpy(piece, value + start, i - start);
            piece[i - start] = '\0';
            if (piece[0] == '/' && vpath_to_native(piece, native, sizeof native) == 0)
                snprintf(piece, sizeof piece, "%s", native);
            if (used > 0)
                out[used++] = ';';
            memcpy(out + used, piece, strlen(piece));
            used += strlen(piece);
        }
        start = i + 1;
    }
    out[used] = '\0';
    return out;
}

#define READONLY_MAX 64
static char* readonlyNames[READONLY_MAX];
static size_t readonlyCount;

int var_is_readonly(const char* name) {
    size_t i;

    for (i = 0; i < readonlyCount; i++)
        if (strcmp(readonlyNames[i], name) == 0)
            return 1;
    return 0;
}

void var_set_readonly(const char* name) {
    if (!var_is_readonly(name) && readonlyCount < READONLY_MAX)
        readonlyNames[readonlyCount++] = _strdup(name);
}

static int set_plain(const char* name, const char* value) {
    Entry* entry;

    if (var_is_readonly(name)) {
        fprintf(stderr, "%s: readonly variable\n", name);
        return -1;
    }
    entry = find_entry(name);

    if (find_array(name) != NULL)
        return put_element(find_array(name), 0, value);     // a=x on an array sets a[0]

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

static int export_plain(const char* name, const char* value);

#define INTEGER_MAX 64
static char* integerNames[INTEGER_MAX];
static size_t integerCount;

int var_is_integer(const char* name) {
    size_t i;

    for (i = 0; i < integerCount; i++)
        if (strcmp(integerNames[i], name) == 0)
            return 1;
    return 0;
}

void var_set_integer(const char* name) {
    if (!var_is_integer(name) && integerCount < INTEGER_MAX)
        integerNames[integerCount++] = _strdup(name);
}

int var_set(const char* name, const char* value) {
    char* converted = _stricmp(name, "PATH") == 0 ? windows_path_list(value) : NULL;
    char number[32];
    int status;

    if (var_is_integer(name) && !var_is_readonly(name)) {
        // declare -i: what is assigned is an arithmetic expression (n=n+2)
        long long result = 0;
        char error[128];

        if (arith_eval(value, &result, error, sizeof error) != 0)
            result = 0;
        snprintf(number, sizeof number, "%lld", result);
        value = number;
    }
    status = set_plain(name, converted != NULL ? converted : value);

    free(converted);
    return status;
}

int var_export(const char* name, const char* value) {
    char* converted = value != NULL && _stricmp(name, "PATH") == 0 ? windows_path_list(value) : NULL;
    int status = export_plain(name, converted != NULL ? converted : value);

    free(converted);
    return status;
}

static int export_plain(const char* name, const char* value) {
    Entry* entry = find_entry(name);

    if (find_array(name) != NULL)
        return 0;                           // an array is not exported

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

void var_each_local(void (*fn)(const char* name, const char* value, void* context), void* context) {
    size_t i;

    for (i = 0; i < entryCount; i++)
        if (entries[i].value != NULL && !entries[i].exported)
            fn(entries[i].name, entries[i].value, context);
}

typedef struct {
    char*  name;
    char** keys;
    long*  indices;
    size_t count;
    long   next;
} Assoc;

static Assoc* assocs;
static size_t assocCount;

static Assoc* find_assoc(const char* name) {
    size_t i;

    for (i = 0; i < assocCount; i++)
        if (strcmp(assocs[i].name, name) == 0)
            return &assocs[i];
    return NULL;
}

int var_is_assoc(const char* name) {
    return find_assoc(name) != NULL;
}

int var_declare_assoc(const char* name) {
    Assoc* grown;

    if (find_assoc(name) != NULL)
        return 0;
    if (var_array_assign(name, NULL, 0) != 0)
        return -1;
    grown = realloc(assocs, (assocCount + 1) * sizeof *grown);
    if (grown == NULL)
        return -1;
    assocs = grown;
    memset(&assocs[assocCount], 0, sizeof assocs[assocCount]);
    assocs[assocCount].name = _strdup(name);
    assocCount++;
    return 0;
}

int var_assoc_index(const char* name, const char* key, int create, long* index) {
    Assoc* assoc = find_assoc(name);
    size_t i;
    char** keys;
    long* indices;

    if (assoc == NULL)
        return -1;
    for (i = 0; i < assoc->count; i++) {
        if (strcmp(assoc->keys[i], key) == 0) {
            *index = assoc->indices[i];
            return 0;
        }
    }
    if (!create) {
        *index = -2147483647L;              // no element has this index
        return 0;
    }
    keys = realloc(assoc->keys, (assoc->count + 1) * sizeof *keys);
    if (keys == NULL)
        return -1;
    assoc->keys = keys;
    indices = realloc(assoc->indices, (assoc->count + 1) * sizeof *indices);
    if (indices == NULL)
        return -1;
    assoc->indices = indices;
    assoc->keys[assoc->count] = _strdup(key);
    assoc->indices[assoc->count] = assoc->next;
    *index = assoc->next++;
    assoc->count++;
    return 0;
}

const char* var_assoc_key(const char* name, long index) {
    Assoc* assoc = find_assoc(name);
    size_t i;

    if (assoc == NULL)
        return NULL;
    for (i = 0; i < assoc->count; i++)
        if (assoc->indices[i] == index)
            return assoc->keys[i];
    return NULL;
}

static void forget_assoc(const char* name) {
    Assoc* assoc = find_assoc(name);
    size_t i;

    if (assoc == NULL)
        return;
    for (i = 0; i < assoc->count; i++)
        free(assoc->keys[i]);
    free(assoc->keys);
    free(assoc->indices);
    free(assoc->name);
    *assoc = assocs[--assocCount];
}

void var_unset(const char* name) {
    Entry* entry;
    ArrayVar* array;

    forget_assoc(name);

    if (var_is_readonly(name)) {
        fprintf(stderr, "unset: %s: cannot unset: readonly variable\n", name);
        return;
    }
    entry = find_entry(name);
    array = find_array(name);

    if (array != NULL)
        remove_array(array);

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
    ArrayVar* arrays;
    size_t arrayCount;
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
    s->arrays = copy_arrays(&s->arrayCount);
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
    free_arrays(arrays, arrayCount);
    arrays = s->arrays;
    arrayCount = s->arrayCount;

    free(s->environment);
    free(s);
}
