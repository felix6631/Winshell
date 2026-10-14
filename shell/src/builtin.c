#include "builtin.h"
#include "interp.h"
#include "command.h"
#include "cwd.h"
#include "vpath.h"
#include "variables.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static void cd_error(const char* target, const char* reason) {
    fprintf(stderr, "cd: %s: %s\n", target, reason);
}

/** Returns 0 when the directory was changed, 1 otherwise. */
static int builtin_cd(char* const argv[]) {
    char absolute[VPATH_MAX];
    const char* target;
    int argc = 0, printTarget = 0;

    while (argv[argc] != NULL)
        argc++;

    if (argc > 2) {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }

    if (argc == 1) {
        target = var_get("USERPROFILE");
        if (target == NULL || target[0] == '\0') {
            fprintf(stderr, "cd: home directory is not set\n");
            return 1;
        }
    } else if (strcmp(argv[1], "-") == 0) {
        target = cwd_previous();
        if (target == NULL) {
            fprintf(stderr, "cd: OLDPWD not set\n");
            return 1;
        }
        printTarget = 1;
    } else {
        target = argv[1];
    }

    switch (cwd_change(target, absolute, sizeof absolute)) {
        case CWD_OK:
            if (printTarget)
                puts(absolute);
            return 0;
        case CWD_NOT_DIRECTORY:
            cd_error(target, "Not a directory");
            return 1;
        case CWD_DENIED:
            cd_error(target, "Permission denied");
            return 1;
        default:
            cd_error(target, "No such file or directory");
            return 1;
    }
}

static int valid_name(const char* name, size_t length) {
    size_t i;
    if (length == 0 || !(isalpha((unsigned char)name[0]) || name[0] == '_'))
        return 0;
    for (i = 1; i < length; i++)
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_'))
            return 0;
    return 1;
}

/** export NAME=VALUE...: the programs started afterwards have the variables too. */
static int builtin_export(char* const argv[]) {
    int i, status = 0;

    if (argv[1] == NULL) {
        // the variables, one per line, as "NAME=value"
        char** entry;
        for (entry = _environ; entry != NULL && *entry != NULL; entry++)
            if ((*entry)[0] != '=')         // the per-drive entries of Windows have no name
                puts(*entry);
        return 0;
    }

    for (i = 1; argv[i] != NULL; i++) {
        const char* equals = strchr(argv[i], '=');
        size_t length = equals != NULL ? (size_t)(equals - argv[i]) : strlen(argv[i]);

        if (!valid_name(argv[i], length)) {
            fprintf(stderr, "export: '%s': not a valid identifier\n", argv[i]);
            status = 1;
            continue;
        }
        {
            char name[256];
            if (length >= sizeof name) {
                fprintf(stderr, "export: '%s': name too long\n", argv[i]);
                status = 1;
                continue;
            }
            memcpy(name, argv[i], length);
            name[length] = '\0';
            // "export NAME" exports the variable the shell has; "export NAME=value" sets it as well
            if (var_export(name, equals != NULL ? equals + 1 : NULL) != 0) {
                fprintf(stderr, "export: out of memory\n");
                status = 1;
            }
        }
    }
    return status;
}

static int builtin_unset(char* const argv[]) {
    int i, status = 0;

    for (i = 1; argv[i] != NULL; i++) {
        if (!valid_name(argv[i], strlen(argv[i]))) {
            fprintf(stderr, "unset: '%s': not a valid identifier\n", argv[i]);
            status = 1;
            continue;
        }
        var_unset(argv[i]);
    }
    return status;
}

int builtin_run(const Command* command, int* status, int* exitShell) {
    const char* name = command->argv[0];

    if (strcmp(name, "cd") == 0) {
        *status = builtin_cd(command->argv);
        return 1;
    }
    if (strcmp(name, "export") == 0) {
        *status = builtin_export(command->argv);
        return 1;
    }
    if (strcmp(name, "unset") == 0) {
        *status = builtin_unset(command->argv);
        return 1;
    }
    if (strcmp(name, ":") == 0) {
        *status = 0;
        return 1;
    }
    if (strcmp(name, "break") == 0 || strcmp(name, "continue") == 0) {
        long levels = 1;

        if (command->argv[1] != NULL) {
            char* end;
            levels = strtol(command->argv[1], &end, 10);
            if (*end != '\0' || levels < 1) {
                fprintf(stderr, "%s: %s: loop count out of range\n", name, command->argv[1]);
                *status = 1;
                return 1;
            }
        }
        *status = command_break((int)levels, name[0] == 'c');
        return 1;
    }
    if (strcmp(name, "source") == 0 || strcmp(name, ".") == 0) {
        if (command->argv[1] == NULL) {
            fprintf(stderr, "%s: filename argument required\n", name);
            *status = 2;
        } else {
            // the arguments after the file become its parameters; without any, it sees those of the caller
            int count = 0;
            while (command->argv[2 + count] != NULL)
                count++;
            *status = interp_run_file(command->argv[1], (const char* const*)command->argv + 2, count, count > 0,
                                      exitShell, *status);
        }
        return 1;
    }
    if (strcmp(name, "exit") == 0 || strcmp(name, "quit") == 0) {
        if (command->argv[1] != NULL) {
            char* end;
            long value = strtol(command->argv[1], &end, 10);
            if (*end != '\0' || command->argv[1][0] == '\0') {
                fprintf(stderr, "%s: %s: numeric argument required\n", name, command->argv[1]);
                *status = 2;
            } else {
                *status = (int)(value & 0xFF);
            }
        }
        *exitShell = 1;
        return 1;
    }
    return 0;
}
