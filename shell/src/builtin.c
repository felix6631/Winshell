#include "builtin.h"
#include "vpath.h"
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

static void builtin_cd(char* const argv[]) {
    char current[VPATH_MAX], absolute[VPATH_MAX], native[VPATH_MAX];
    const char* target;
    int argc = 0, kind, printTarget = 0;

    while (argv[argc] != NULL)
        argc++;

    if (argc > 2) {
        fprintf(stderr, "cd: too many arguments\n");
        return;
    }

    if (argc == 1) {
        target = getenv("USERPROFILE");
        if (target == NULL || target[0] == '\0') {
            fprintf(stderr, "cd: home directory is not set\n");
            return;
        }
    } else if (strcmp(argv[1], "-") == 0) {
        target = getenv("OLDPWD");
        if (target == NULL || target[0] == '\0') {
            fprintf(stderr, "cd: OLDPWD not set\n");
            return;
        }
        printTarget = 1;
    } else {
        target = argv[1];
    }

    if (vpath_getcwd(current, sizeof current) != 0)
        strcpy(current, "/");

    if (vpath_resolve(current, target, absolute, sizeof absolute) != 0) {
        cd_error(target, "No such file or directory");
        return;
    }

    kind = vpath_absolute_to_native(absolute, native, sizeof native);
    if (kind == VPATH_ROOT) {
        // the root has no directory of its own; the process waits on the system drive
        if (vpath_root_native(native, sizeof native) != 0) {
            cd_error(target, "No such file or directory");
            return;
        }
    } else if (kind != 0) {
        cd_error(target, "No such file or directory");
        return;
    } else {
        DWORD attributes = GetFileAttributesA(native);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            cd_error(target, "No such file or directory");
            return;
        }
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            cd_error(target, "Not a directory");
            return;
        }
    }

    if (!SetCurrentDirectoryA(native)) {
        cd_error(target, "Permission denied");
        return;
    }

    _putenv_s("OLDPWD", current);
    _putenv_s("PWD", absolute);
    if (printTarget)
        puts(absolute);
}

int builtin_run(const Command* command) {
    if (strcmp(command->argv[0], "cd") == 0) {
        builtin_cd(command->argv);
        return 1;
    }
    return 0;
}
