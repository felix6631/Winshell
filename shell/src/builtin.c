#include "builtin.h"
#include "vpath.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

void builtin_init(void) {
    char cwd[VPATH_MAX];
    if (vpath_getcwd(cwd, sizeof cwd) == 0)
        _putenv_s("PWD", cwd);
}

static void cd_error(const char* target, const char* reason) {
    fprintf(stderr, "cd: %s: %s\n", target, reason);
}

/** Returns 0 when the directory was changed, 1 otherwise. */
static int builtin_cd(char* const argv[]) {
    char current[VPATH_MAX], absolute[VPATH_MAX], native[VPATH_MAX];
    const char* target;
    int argc = 0, kind, printTarget = 0;

    while (argv[argc] != NULL)
        argc++;

    if (argc > 2) {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }

    if (argc == 1) {
        target = getenv("USERPROFILE");
        if (target == NULL || target[0] == '\0') {
            fprintf(stderr, "cd: home directory is not set\n");
            return 1;
        }
    } else if (strcmp(argv[1], "-") == 0) {
        target = getenv("OLDPWD");
        if (target == NULL || target[0] == '\0') {
            fprintf(stderr, "cd: OLDPWD not set\n");
            return 1;
        }
        printTarget = 1;
    } else {
        target = argv[1];
    }

    if (vpath_getcwd(current, sizeof current) != 0)
        strcpy(current, "/");

    if (vpath_resolve(current, target, absolute, sizeof absolute) != 0) {
        cd_error(target, "No such file or directory");
        return 1;
    }

    kind = vpath_absolute_to_native(absolute, native, sizeof native);
    if (kind == VPATH_ROOT) {
        // the root has no directory of its own; the process waits on the system drive
        if (vpath_root_native(native, sizeof native) != 0) {
            cd_error(target, "No such file or directory");
            return 1;
        }
    } else if (kind != 0) {
        cd_error(target, "No such file or directory");
        return 1;
    } else {
        DWORD attributes = GetFileAttributesA(native);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            cd_error(target, "No such file or directory");
            return 1;
        }
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            cd_error(target, "Not a directory");
            return 1;
        }
    }

    if (!SetCurrentDirectoryA(native)) {
        cd_error(target, "Permission denied");
        return 1;
    }

    _putenv_s("OLDPWD", current);
    _putenv_s("PWD", absolute);
    if (printTarget)
        puts(absolute);
    return 0;
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
        if (equals != NULL) {
            char name[256];
            if (length >= sizeof name) {
                fprintf(stderr, "export: '%s': name too long\n", argv[i]);
                status = 1;
                continue;
            }
            memcpy(name, argv[i], length);
            name[length] = '\0';
            if (equals[1] == '\0') {
                // _putenv_s with "" removes the variable; Windows can hold it empty
                _putenv_s(name, "");
                SetEnvironmentVariableA(name, "");
            } else {
                _putenv_s(name, equals + 1);
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
        _putenv_s(argv[i], "");      // an empty value removes the variable
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
