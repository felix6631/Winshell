#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "cmdline.h"
#include "vpath.h"
#include "program.h"
#include "args.h"
#include "vec.h"

#define VERSION "0.1"

static void print_usage(void) {
    puts("Usage: env [OPTION]... [NAME=VALUE]... [COMMAND [ARG]...]\n"
         "Set each NAME to VALUE in the environment and run COMMAND, or print the\n"
         "environment when there is no COMMAND.\n"
         "\n"
         "  -i, --ignore-environment  start with an empty environment\n"
         "  -u, --unset=NAME          remove NAME from the environment\n"
         "  -0, --null                end each output line with NUL, not newline\n"
         "  -C, --chdir=DIR           change the working directory to DIR (a virtual path)\n"
         "      --help                display this help and exit\n"
         "      --version             output version information and exit\n"
         "\n"
         "COMMAND is a program of the directory of env.exe (\".exe\" may be left out),\n"
         "or a path of the virtual file system.\n"
         "Exit status: that of COMMAND; 127 when it is not found; 125 when env itself failed.");
}

/* the environment being built: pointers to NAME=value strings */
typedef struct {
    char** items;
    size_t count, capacity;
} Environment;

static int env_push(Environment* e, char* entry) {
    if (VEC_RESERVE(e->items, e->capacity, e->count + 1) != 0)
        return -1;
    e->items[e->count++] = entry;
    return 0;
}

/** The index of the variable with the name of length length (names are not case sensitive), or -1. */
static long env_find(const Environment* e, const char* name, size_t length) {
    size_t i;
    for (i = 0; i < e->count; i++) {
        const char* entry = e->items[i];
        if (strncmp(entry, "=", 1) != 0 && _strnicmp(entry, name, length) == 0 && entry[length] == '=')
            return (long)i;
    }
    return -1;
}

static void env_remove(Environment* e, const char* name, size_t length) {
    long index = env_find(e, name, length);
    if (index >= 0) {
        memmove(&e->items[index], &e->items[index + 1], (e->count - (size_t)index - 1) * sizeof *e->items);
        e->count--;
    }
}

/** Sets NAME=value; entry has to stay allocated. */
static int env_set(Environment* e, char* entry) {
    size_t length = strcspn(entry, "=");
    long index = env_find(e, entry, length);
    if (index >= 0) {
        e->items[index] = entry;
        return 0;
    }
    return env_push(e, entry);
}

static int is_assignment(const char* word) {
    const char* equals = strchr(word, '=');
    return equals != NULL && equals != word;
}

static int run(char* const argv[], const Environment* e) {
    char path[MAX_PATH * 2];
    char* command;
    char* block;
    size_t size = 2, used = 0, i;       // an empty environment is two NULs
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code;

    if (program_find(argv[0], path, sizeof path) != 0) {
        fprintf(stderr, "env: '%s': No such file or directory\n", argv[0]);
        return 127;
    }

    for (i = 0; i < e->count; i++)
        size += strlen(e->items[i]) + 1;
    block = malloc(size);
    command = cmdline_build(argv);
    if (block == NULL || command == NULL) {
        fprintf(stderr, "env: out of memory\n");
        free(block);
        free(command);
        return 125;
    }
    for (i = 0; i < e->count; i++) {
        size_t length = strlen(e->items[i]) + 1;
        memcpy(block + used, e->items[i], length);
        used += length;
    }
    block[used] = '\0';
    block[used + 1] = '\0';

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    SetHandleInformation(si.hStdInput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(si.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(si.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    fflush(stdout);
    if (!CreateProcessA(path, command, NULL, NULL, TRUE, 0, block, NULL, &si, &pi)) {
        fprintf(stderr, "env: cannot run '%s'\n", argv[0]);
        free(block);
        free(command);
        return 126;
    }
    free(block);
    free(command);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (!GetExitCodeProcess(pi.hProcess, &code))
        code = 125;
    CloseHandle(pi.hProcess);
    return (int)code;
}

enum { OPT_IGNORE = 1, OPT_UNSET, OPT_NULL, OPT_CHDIR };

static const ArgsOption options[] = {
    { OPT_IGNORE, 'i', "ignore-environment", ARGS_NONE },
    { OPT_UNSET,  'u', "unset",              ARGS_REQUIRED },
    { OPT_NULL,   '0', "null",               ARGS_NONE },
    { OPT_CHDIR,  'C', "chdir",              ARGS_REQUIRED },
    { 0 }
};

// 125 is the status of env itself failing; the first operand ends the options
static const ArgsSpec spec = { "env", VERSION, print_usage, options, 125, 1, 0 };

typedef struct {
    Environment* environment;
    const char*  chdirTo;
    int          nullEnd;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
        case OPT_IGNORE:
            o->environment->count = 0;
            return 0;
        case OPT_NULL:
            o->nullEnd = 1;
            return 0;
        case OPT_CHDIR:
            o->chdirTo = value;
            return 0;
        default:
            if (value[0] == '\0' || strchr(value, '=') != NULL) {
                fprintf(stderr, "env: cannot unset '%s': Invalid argument\n", value);
                return -1;
            }
            env_remove(o->environment, value, strlen(value));
            return 0;
    }
}

int main(int argc, char* argv[]) {
    Environment e = { NULL, 0, 0 };
    Options o;
    char* strings;
    const char* chdirTo;
    const char** operands;
    int nullEnd, i = 0, count, result;
    const char* p;

    strings = GetEnvironmentStringsA();
    if (strings == NULL) {
        fprintf(stderr, "env: cannot read the environment\n");
        return 125;
    }
    for (p = strings; *p != '\0'; p += strlen(p) + 1)
        if (*p != '=' && env_push(&e, (char*)p) != 0)
            goto err_memory;

    o.environment = &e;
    o.chdirTo = NULL;
    o.nullEnd = 0;
    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    chdirTo = o.chdirTo;
    nullEnd = o.nullEnd;

    // NAME=VALUE words, then the command
    for (; i < count && is_assignment(operands[i]); i++)
        if (env_set(&e, (char*)operands[i]) != 0)
            goto err_memory;

    if (i >= count) {
        size_t k;
        if (chdirTo != NULL) {
            fprintf(stderr, "env: must specify a command with --chdir (-C)\n");
            return 125;
        }
        _setmode(_fileno(stdout), _O_BINARY);
        for (k = 0; k < e.count; k++) {
            fputs(e.items[k], stdout);
            putchar(nullEnd ? '\0' : '\n');
        }
        return ferror(stdout) ? 125 : 0;
    }

    if (nullEnd) {
        fprintf(stderr, "env: cannot specify --null (-0) with command\n");
        return 125;
    }

    if (chdirTo != NULL) {
        char native[VPATH_MAX], virtualPath[VPATH_MAX], pwd[VPATH_MAX + 8];
        DWORD length;

        if (vpath_to_native(chdirTo, native, sizeof native) != 0 || !SetCurrentDirectoryA(native)) {
            fprintf(stderr, "env: cannot change directory to '%s': No such file or directory\n", chdirTo);
            return 125;
        }
        // the programs find the directory through PWD, which has to describe the new one
        length = GetCurrentDirectoryA(sizeof native, native);
        if (length > 0 && length < sizeof native && vpath_from_native(native, virtualPath, sizeof virtualPath) == 0) {
            snprintf(pwd, sizeof pwd, "PWD=%s", virtualPath);
            if (env_set(&e, _strdup(pwd)) != 0)
                goto err_memory;
        }
    }

    return run((char* const*)&operands[i], &e);

err_memory:
    fprintf(stderr, "env: out of memory\n");
    return 125;
}
