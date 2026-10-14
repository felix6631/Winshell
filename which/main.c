/*
 * which: where the program of a name is. A name without a path is looked for the way the shell finds a
 * program: in the directory of the programs of this project first, then in the directories of PATH, with
 * the extensions of PATHEXT added when the name has none of them. A name with a path is checked as it is.
 * The result is a path of the virtual file system. The exit status is 0 when every name was found, 1 when
 * one was not, 2 for a wrong option.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "program.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "which"

static void print_usage(void) {
    puts("Usage: which [OPTION]... PROGRAM...\n"
         "Write the full path of the programs that would be run for the given names.\n"
         "\n"
         "  -a, --all       print all matches in PATH, not only the first\n"
         "  -s, --silent    print nothing; the exit status says whether all were found\n"
         "      --help      display this help and exit\n"
         "      --version   output version information and exit\n"
         "\n"
         "A name is looked for in the directory of the programs of this project, then in the\n"
         "directories of PATH; the extensions of PATHEXT (.COM .EXE .BAT .CMD by default) are tried\n"
         "when the name has none. A name with a / or a \\ is checked as it is.\n"
         "\n"
         "Exit status: 0 if all names were found, 1 if at least one was not, 2 for a wrong option.");
}

enum { OPT_ALL = 1, OPT_SILENT };

static const ArgsOption options[] = {
    { OPT_ALL,    'a', "all",    ARGS_NONE },
    { OPT_SILENT, 's', "silent", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 2, 0, 0 };

typedef struct {
    int all;
    int silent;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    (void)value;
    if (id == OPT_ALL)
        o->all = 1;
    else if (id == OPT_SILENT)
        o->silent = 1;
    return 0;
}

/** The names of the extensions to try: PATHEXT as a list ".COM", ".EXE", ... */
typedef struct {
    char items[16][16];
    int  count;
} Extensions;

static void load_extensions(Extensions* e) {
    const char* value = getenv("PATHEXT");
    char copy[256];
    char* token;
    char* next = NULL;

    e->count = 0;
    snprintf(copy, sizeof copy, "%s", value != NULL && value[0] != '\0' ? value : ".COM;.EXE;.BAT;.CMD");
    for (token = strtok_s(copy, ";", &next); token != NULL && e->count < 16; token = strtok_s(NULL, ";", &next))
        if (token[0] == '.' && strlen(token) < sizeof e->items[0])
            snprintf(e->items[e->count++], sizeof e->items[0], "%s", token);
}

static int is_file(const char* path) {
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int has_extension(const char* name) {
    const char* dot = strrchr(name, '.');
    const char* slash = strrchr(name, '\\');
    return dot != NULL && (slash == NULL || dot > slash) && dot[1] != '\0';
}

typedef struct {
    char (*found)[VPATH_MAX];
    int  count;
    int  capacity;
} Matches;

static int already_found(const Matches* m, const char* path) {
    int i;
    for (i = 0; i < m->count; i++)
        if (_stricmp(m->found[i], path) == 0)
            return 1;
    return 0;
}

/** The path with the last component spelled as the file system has it (cmd.EXE is cmd.exe on disk). */
static void real_spelling(const char* path, char* out, size_t size) {
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(path, &data);
    const char* slash;

    snprintf(out, size, "%s", path);
    if (find == INVALID_HANDLE_VALUE)
        return;
    FindClose(find);
    slash = strrchr(path, '\\');
    if (slash != NULL)
        snprintf(out, size, "%.*s%s", (int)(slash - path + 1), path, data.cFileName);
    else
        snprintf(out, size, "%s", data.cFileName);
}

static void add_match(Matches* m, const char* path) {
    char spelled[VPATH_MAX];

    real_spelling(path, spelled, sizeof spelled);
    if (already_found(m, spelled) || m->count >= m->capacity)
        return;
    snprintf(m->found[m->count++], VPATH_MAX, "%s", spelled);
}

/** A file directory\name as it is, or with one of the extensions; true when one exists. */
static void check_in(Matches* m, const char* directory, const char* name, const Extensions* ext, int all) {
    char path[VPATH_MAX];
    int i;

    if (directory != NULL)
        snprintf(path, sizeof path, "%s%s%s", directory,
                 directory[0] != '\0' && directory[strlen(directory) - 1] != '\\' ? "\\" : "", name);
    else
        snprintf(path, sizeof path, "%s", name);

    // the name as it is counts when it has an extension that PATHEXT lists, or when it is a path
    if (has_extension(name) && is_file(path)) {
        add_match(m, path);
        if (!all)
            return;
    }
    for (i = 0; i < ext->count; i++) {
        char with[VPATH_MAX];
        snprintf(with, sizeof with, "%s%s", path, ext->items[i]);
        if (is_file(with)) {
            add_match(m, with);
            if (!all)
                return;
        }
    }
    if (!has_extension(name) && is_file(path) && ext->count == 0)
        add_match(m, path);
}

static void find_program(Matches* m, const char* name, const Extensions* ext, int all) {
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        char native[VPATH_MAX];
        if (vpath_to_native(name, native, sizeof native) == 0)
            check_in(m, NULL, native, ext, all);
        return;
    }

    {
        char path[VPATH_MAX];
        if (program_find(name, path, sizeof path) == 0 && is_file(path)) {
            add_match(m, path);
            if (!all)
                return;
        }
    }

    {
        const char* value = getenv("PATH");
        char* copy;
        char* token;
        char* next = NULL;

        if (value == NULL)
            return;
        copy = _strdup(value);
        if (copy == NULL)
            return;
        for (token = strtok_s(copy, ";", &next); token != NULL; token = strtok_s(NULL, ";", &next)) {
            size_t n = strlen(token);
            char directory[VPATH_MAX];

            if (n == 0 || n >= sizeof directory)
                continue;
            // a quoted directory
            if (token[0] == '"' && token[n - 1] == '"' && n >= 2) {
                memcpy(directory, token + 1, n - 2);
                directory[n - 2] = '\0';
            } else {
                snprintf(directory, sizeof directory, "%s", token);
            }
            check_in(m, directory, name, ext, all);
            if (!all && m->count > 0)
                break;
        }
        free(copy);
    }
}

int main(int argc, char* argv[]) {
    Options o;
    const char** operands;
    int count, result, i, status = 0;
    Extensions ext;
    Matches matches;

    memset(&o, 0, sizeof o);
    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    if (count == 0) {
        // like GNU which: the usage, and a status that no name was looked for with
        print_usage();
        return 255;
    }

    load_extensions(&ext);
    matches.capacity = 64;
    matches.found = malloc((size_t)matches.capacity * sizeof *matches.found);
    if (matches.found == NULL)
        return 2;

    _setmode(_fileno(stdout), _O_BINARY);
    for (i = 0; i < count; i++) {
        int k;

        matches.count = 0;
        find_program(&matches, operands[i], &ext, o.all);
        if (matches.count == 0) {
            status = 1;
            if (!o.silent)
                fprintf(stderr, "%s: no %s in (%s)\n", PROGRAM, operands[i], getenv("PATH") != NULL ? getenv("PATH") : "");
            continue;
        }
        if (o.silent)
            continue;
        for (k = 0; k < matches.count; k++) {
            char shown[VPATH_MAX];
            if (vpath_from_native(matches.found[k], shown, sizeof shown) != 0)
                snprintf(shown, sizeof shown, "%s", matches.found[k]);
            fputs(shown, stdout);
            putchar('\n');
        }
    }
    free(matches.found);
    return fflush(stdout) != 0 ? 2 : status;
}
