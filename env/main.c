#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "cmdline.h"
#include "vpath.h"

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
    if (e->count == e->capacity) {
        size_t capacity = e->capacity == 0 ? 64 : e->capacity * 2;
        char** grown = realloc(e->items, capacity * sizeof *grown);
        if (grown == NULL)
            return -1;
        e->items = grown;
        e->capacity = capacity;
    }
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

/** The program to start: a path of the virtual file system, or a name in the directory of env.exe. */
static int resolve_program(const char* program, char* path, size_t size) {
    size_t length = strlen(program);
    int written;

    if (strchr(program, '/') != NULL || strchr(program, '\\') != NULL) {
        char native[VPATH_MAX];
        if (vpath_to_native(program, native, sizeof native) != 0)
            return -1;
        written = snprintf(path, size, "%s", native);
        if (written >= 0 && (size_t)written < size && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES &&
            !(length >= 4 && _stricmp(program + length - 4, ".exe") == 0))
            written = snprintf(path, size, "%s.exe", native);
    } else {
        char directory[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, directory, MAX_PATH);
        char* separator;
        if (n == 0 || n >= MAX_PATH)
            return -1;
        separator = strrchr(directory, '\\');
        if (separator == NULL)
            return -1;
        *separator = '\0';
        if (length >= 4 && _stricmp(program + length - 4, ".exe") == 0)
            written = snprintf(path, size, "%s\\%s", directory, program);
        else
            written = snprintf(path, size, "%s\\%s.exe", directory, program);
    }
    if (written < 0 || (size_t)written >= size)
        return -1;
    return GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES ? -1 : 0;
}

static int run(char* const argv[], const Environment* e) {
    char path[MAX_PATH * 2];
    char* command;
    char* block;
    size_t size = 1, used = 0, i;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code;

    if (resolve_program(argv[0], path, sizeof path) != 0) {
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
    if (e->count == 0) {
        // an empty block is two NULs
        block = realloc(block, 2);
        block[0] = block[1] = '\0';
    }

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

int main(int argc, char* argv[]) {
    Environment e = { NULL, 0, 0 };
    char* strings;
    const char* chdirTo = NULL;
    int nullEnd = 0, i = 1;
    const char* p;

    strings = GetEnvironmentStringsA();
    if (strings == NULL) {
        fprintf(stderr, "env: cannot read the environment\n");
        return 125;
    }
    for (p = strings; *p != '\0'; p += strlen(p) + 1)
        if (*p != '=' && env_push(&e, (char*)p) != 0)
            goto err_memory;

    // options, in the order given; they end at the first word that is not one
    for (; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }
        if (arg[0] != '-' || arg[1] == '\0')
            break;

        if (strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(arg, "--version") == 0) {
            puts("env (Winshell) " VERSION);
            return 0;
        } else if (strcmp(arg, "--ignore-environment") == 0) {
            e.count = 0;
        } else if (strcmp(arg, "--null") == 0) {
            nullEnd = 1;
        } else if (strncmp(arg, "--unset=", 8) == 0 || strcmp(arg, "--unset") == 0) {
            const char* name = arg[7] == '=' ? arg + 8 : (++i < argc ? argv[i] : NULL);
            if (name == NULL || name[0] == '\0' || strchr(name, '=') != NULL) {
                fprintf(stderr, "env: cannot unset '%s': Invalid argument\n", name != NULL ? name : "");
                return 125;
            }
            env_remove(&e, name, strlen(name));
        } else if (strncmp(arg, "--chdir=", 8) == 0 || strcmp(arg, "--chdir") == 0) {
            chdirTo = arg[7] == '=' ? arg + 8 : (++i < argc ? argv[i] : NULL);
            if (chdirTo == NULL) {
                fprintf(stderr, "env: option '--chdir' requires an argument\n");
                return 125;
            }
        } else if (arg[1] != '-') {
            // short options, which may be grouped; -u and -C take the rest or the next word
            const char* o;
            for (o = arg + 1; *o != '\0'; o++) {
                if (*o == 'i') {
                    e.count = 0;
                } else if (*o == '0') {
                    nullEnd = 1;
                } else if (*o == 'u' || *o == 'C') {
                    const char* value = o[1] != '\0' ? o + 1 : (++i < argc ? argv[i] : NULL);
                    if (value == NULL) {
                        fprintf(stderr, "env: option requires an argument -- '%c'\n", *o);
                        return 125;
                    }
                    if (*o == 'C') {
                        chdirTo = value;
                    } else {
                        if (value[0] == '\0' || strchr(value, '=') != NULL) {
                            fprintf(stderr, "env: cannot unset '%s': Invalid argument\n", value);
                            return 125;
                        }
                        env_remove(&e, value, strlen(value));
                    }
                    break;
                } else {
                    fprintf(stderr, "env: invalid option -- '%c'\nTry 'env --help' for more information.\n", *o);
                    return 125;
                }
            }
        } else {
            fprintf(stderr, "env: unrecognized option '%s'\nTry 'env --help' for more information.\n", arg);
            return 125;
        }
    }

    // NAME=VALUE words, then the command
    for (; i < argc && is_assignment(argv[i]); i++)
        if (env_set(&e, argv[i]) != 0)
            goto err_memory;

    if (i >= argc) {
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

    return run(&argv[i], &e);

err_memory:
    fprintf(stderr, "env: out of memory\n");
    return 125;
}
