#include "builtin.h"
#include "interp.h"
#include "vec.h"
#include "command.h"
#include "cwd.h"
#include "jobs.h"
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

/**
 * read [-r] [-p PROMPT] [NAME...]: one line of the input, cut at the characters of IFS (blanks by default) into
 * the variables; the last one gets the rest. Without a name the line goes to REPLY. A backslash makes the next
 * character plain, and a backslash at the end joins the next line (-r: no backslashes). Status 0, or 1 at the
 * end of the input (the variables are set from what was read).
 */
static int builtin_read(char* const argv[]) {
    int raw = 0, i, count, status = 0, gotAny = 0;
    const char* prompt = NULL;
    const char* ifs;
    char* line = NULL;
    char* text = NULL;
    size_t length = 0, capacity = 0, position = 0;
    char* const* names;

    for (i = 1; argv[i] != NULL && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        const char* option = argv[i] + 1;

        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        for (; *option != '\0'; option++) {
            if (*option == 'r') {
                raw = 1;
            } else if (*option == 's') {
                // no echo: nothing to do here
            } else if (*option == 'p') {
                if (option[1] != '\0')
                    prompt = option + 1;
                else if (argv[i + 1] != NULL)
                    prompt = argv[++i];
                break;
            } else {
                fprintf(stderr, "read: -%c: invalid option\n", *option);
                return 2;
            }
        }
    }
    names = argv + i;
    for (count = 0; names[count] != NULL; count++) {
        const char* n = names[count];
        size_t k;

        if (!(isalpha((unsigned char)n[0]) || n[0] == '_')) {
            fprintf(stderr, "read: '%s': not a valid identifier\n", n);
            return 1;
        }
        for (k = 0; n[k] != '\0'; k++) {
            if (!(isalnum((unsigned char)n[k]) || n[k] == '_')) {
                fprintf(stderr, "read: '%s': not a valid identifier\n", n);
                return 1;
            }
        }
    }

    if (prompt != NULL) {
        DWORD mode;

        if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode)) {
            fputs(prompt, stderr);
            fflush(stderr);
        }
    }

    // the line, with the continuation lines of a trailing backslash
    for (;;) {
        size_t n;

        if (!interp_read_line(&line)) {
            status = 1;
            break;
        }
        gotAny = 1;
        n = strlen(line);
        if (VEC_RESERVE(text, capacity, length + n + 2) != 0) {
            free(line);
            return 1;
        }
        if (!raw && n > 0 && line[n - 1] == '\\') {
            memcpy(text + length, line, n - 1);
            length += n - 1;
            free(line);
            continue;
        }
        memcpy(text + length, line, n);
        length += n;
        free(line);
        break;
    }
    if (text == NULL) {
        text = malloc(1);
        if (text == NULL)
            return 1;
    }
    text[length] = '\0';
    (void)gotAny;

    ifs = var_get("IFS");
    if (ifs == NULL)
        ifs = " \t\n";

    if (count == 0) {
        var_set("REPLY", text);
    } else {
        for (i = 0; i < count; i++) {
            size_t start, end, k, out = 0;
            int last = i == count - 1;
            char* value;

            // blanks in IFS in front of a field go
            while (position < length && strchr(ifs, text[position]) != NULL && strchr(" \t\n", text[position]) != NULL)
                position++;
            start = position;
            end = start;
            if (last) {
                end = length;
                while (end > start && strchr(" \t\n", text[end - 1]) != NULL && strchr(ifs, text[end - 1]) != NULL)
                    end--;
                position = length;
            } else {
                while (end < length && strchr(ifs, text[end]) == NULL) {
                    if (!raw && text[end] == '\\' && end + 1 < length)
                        end++;
                    end++;
                }
                position = end;
                if (position < length)
                    position++;             // the separator
            }
            value = malloc(end - start + 1);
            if (value == NULL) {
                free(text);
                return 1;
            }
            for (k = start; k < end; k++) {
                if (!raw && text[k] == '\\' && k + 1 < end)
                    k++;
                value[out++] = text[k];
            }
            value[out] = '\0';
            var_set(names[i], value);
            free(value);
        }
    }
    free(text);
    return status;
}

static int builtin_unset(char* const argv[]) {
    int i, status = 0, functions = 0;

    for (i = 1; argv[i] != NULL; i++) {
        if (strcmp(argv[i], "-f") == 0) {
            functions = 1;
            continue;
        }
        if (strcmp(argv[i], "-v") == 0) {
            functions = 0;
            continue;
        }
        if (functions) {
            command_unset_function(argv[i]);
            continue;
        }
        if (!valid_name(argv[i], strlen(argv[i]))) {
            fprintf(stderr, "unset: '%s': not a valid identifier\n", argv[i]);
            status = 1;
            continue;
        }
        var_unset(argv[i]);
    }
    return status;
}

int builtin_exists(const char* name) {
    static const char* const names[] = { "cd", "export", "unset", "exit", "quit", "source", ".", ":", "break",
                                         "continue", "read", "return", "local", "trap", "jobs", "wait", "shift", "fg", "bg", "disown" };
    size_t i;

    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strcmp(name, names[i]) == 0)
            return 1;
    return 0;
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
    if (strcmp(name, "read") == 0) {
        *status = builtin_read(command->argv);
        return 1;
    }
    if (strcmp(name, "return") == 0) {
        int value = *status;

        if (command->argv[1] != NULL) {
            char* end;
            long number = strtol(command->argv[1], &end, 10);

            if (*end != '\0' || command->argv[1][0] == '\0') {
                fprintf(stderr, "return: %s: numeric argument required\n", command->argv[1]);
                number = 2;
            }
            value = (int)(number & 0xFF);
        }
        *status = command_return(value) == 0 ? value : 1;
        return 1;
    }
    if (strcmp(name, "local") == 0) {
        int i, failed = 0;

        for (i = 1; command->argv[i] != NULL; i++) {
            const char* equals = strchr(command->argv[i], '=');
            char variable[256];
            size_t length = equals != NULL ? (size_t)(equals - command->argv[i]) : strlen(command->argv[i]);

            if (length == 0 || length >= sizeof variable || !valid_name(command->argv[i], length)) {
                fprintf(stderr, "local: '%s': not a valid identifier\n", command->argv[i]);
                failed = 1;
                continue;
            }
            memcpy(variable, command->argv[i], length);
            variable[length] = '\0';
            if (command_local(variable, equals != NULL ? equals + 1 : NULL) != 0)
                failed = 1;
        }
        *status = failed;
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
    if (strcmp(name, "shift") == 0) {
        long n = 1;
        int count = winshell_positional_count();

        if (command->argv[1] != NULL) {
            char* end;

            n = strtol(command->argv[1], &end, 10);
            if (*end != '\0' || command->argv[1][0] == '\0' || n < 0) {
                fprintf(stderr, "shift: %s: numeric argument required\n", command->argv[1]);
                *status = 2;
                return 1;
            }
        }
        if (n > count) {
            fprintf(stderr, "shift: shift count out of range\n");
            *status = 1;
            return 1;
        }
        {
            const char** rest = calloc((size_t)(count - n) + 1, sizeof *rest);
            int i;

            if (rest == NULL) {
                *status = 1;
                return 1;
            }
            for (i = (int)n; i < count; i++)
                rest[i - n] = winshell_positional(i + 1);
            interp_set_parameters(NULL, rest, count - (int)n);
            free(rest);
        }
        *status = 0;
        return 1;
    }
    if (strcmp(name, "jobs") == 0) {
        *status = jobs_builtin_jobs(command->argv);
        return 1;
    }
    if (strcmp(name, "fg") == 0) {
        *status = jobs_builtin_fg(command->argv);
        return 1;
    }
    if (strcmp(name, "bg") == 0) {
        *status = jobs_builtin_bg(command->argv);
        return 1;
    }
    if (strcmp(name, "disown") == 0) {
        *status = jobs_builtin_disown(command->argv);
        return 1;
    }
    if (strcmp(name, "wait") == 0) {
        *status = jobs_builtin_wait(command->argv);
        return 1;
    }
    if (strcmp(name, "trap") == 0) {
        *status = command_trap(command->argv);
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
