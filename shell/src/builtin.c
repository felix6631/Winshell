#include "builtin.h"
#include "interp.h"
#include "vec.h"
#include "command.h"
#include "cwd.h"
#include "jobs.h"
#include "vpath.h"
#include "variables.h"
#include "program.h"
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
    const char* arrayName = NULL;
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
            } else if (*option == 'a') {
                if (option[1] != '\0')
                    arrayName = option + 1;
                else if (argv[i + 1] != NULL)
                    arrayName = argv[++i];
                break;
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
    if (interp_lastLineUnterminated)
        status = 1;                         // the variables are set, but read fails: a loop on it ends

    ifs = var_get("IFS");
    if (ifs == NULL)
        ifs = " \t\n";

    if (arrayName != NULL) {
        // read -a NAME: every field of the line is an element
        char** fields = NULL;
        size_t fieldCount = 0, fieldCapacity = 0, at = 0;

        while (at < length) {
            size_t out = 0;
            char* value;

            while (at < length && strchr(ifs, text[at]) != NULL && strchr(" \t\n", text[at]) != NULL)
                at++;
            if (at >= length)
                break;
            value = malloc(length - at + 1);
            if (value == NULL)
                break;
            while (at < length && strchr(ifs, text[at]) == NULL) {
                if (!raw && text[at] == '\\' && at + 1 < length)
                    at++;
                value[out++] = text[at++];
            }
            value[out] = '\0';
            if (at < length)
                at++;                       // the separator
            if (VEC_RESERVE(fields, fieldCapacity, fieldCount + 2) != 0) {
                free(value);
                break;
            }
            fields[fieldCount++] = value;
        }
        var_array_assign(arrayName, fields, fieldCount);
        while (fieldCount > 0)
            free(fields[--fieldCount]);
        free(fields);
    } else if (count == 0) {
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
        {
            const char* bracket = strchr(argv[i], '[');
            size_t end = bracket != NULL ? winshell_subscript_end(bracket) : 0;

            if (bracket != NULL && end > 0 && bracket[end + 1] == '\0' && valid_name(argv[i], (size_t)(bracket - argv[i]))) {
                // unset NAME[index]
                char name[256];
                char* subscript = malloc(end);
                long index;

                if (subscript == NULL || (size_t)(bracket - argv[i]) >= sizeof name) {
                    free(subscript);
                    status = 1;
                    continue;
                }
                memcpy(name, argv[i], (size_t)(bracket - argv[i]));
                name[bracket - argv[i]] = '\0';
                memcpy(subscript, bracket + 1, end - 1);
                subscript[end - 1] = '\0';
                if (winshell_subscript_index(name, subscript, 0, &index) == 0)
                    var_array_unset(name, index);
                else
                    status = 1;
                free(subscript);
                continue;
            }
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

/* ---- eval, let, readonly, declare, type, command, getopts, shopt ---------------------------------------- */

/** The arguments from `first` joined with blanks; malloc'ed. */
static char* join_arguments(char* const argv[], int first) {
    size_t length = 1;
    int i;
    char* text;

    for (i = first; argv[i] != NULL; i++)
        length += strlen(argv[i]) + 1;
    text = malloc(length);
    if (text == NULL)
        return NULL;
    text[0] = '\0';
    for (i = first; argv[i] != NULL; i++) {
        if (i > first)
            strcat(text, " ");
        strcat(text, argv[i]);
    }
    return text;
}

/** A word that the parser reads back as it is: in single quotes. malloc'ed. */
static char* quote_word(const char* word) {
    size_t extra = 2, i;
    char* out;
    size_t n = 0;

    for (i = 0; word[i] != '\0'; i++)
        extra += word[i] == '\'' ? 4 : 1;
    out = malloc(extra + 1);
    if (out == NULL)
        return NULL;
    out[n++] = '\'';
    for (i = 0; word[i] != '\0'; i++) {
        if (word[i] == '\'') {
            memcpy(out + n, "'\\''", 4);
            n += 4;
        } else {
            out[n++] = word[i];
        }
    }
    out[n++] = '\'';
    out[n] = '\0';
    return out;
}

static int builtin_eval(char* const argv[], int* status, int* exitShell) {
    char* text;

    if (argv[1] == NULL)
        return 0;
    text = join_arguments(argv, 1);
    if (text == NULL)
        return 1;
    *status = interp_run_line(text, exitShell, *status);
    free(text);
    return *status;
}

static int builtin_let(char* const argv[]) {
    int i, status = 1;

    if (argv[1] == NULL) {
        fprintf(stderr, "let: expression expected\n");
        return 1;
    }
    for (i = 1; argv[i] != NULL; i++)
        status = command_arithmetic(argv[i]);
    return status;
}

static int builtin_readonly(char* const argv[]) {
    int i, status = 0;

    for (i = 1; argv[i] != NULL; i++) {
        const char* equals;
        char name[256];
        size_t length;

        if (argv[i][0] == '-')
            continue;                       // -p, -a, -f: nothing to say here
        equals = strchr(argv[i], '=');
        length = equals != NULL ? (size_t)(equals - argv[i]) : strlen(argv[i]);
        if (length == 0 || length >= sizeof name || !valid_name(argv[i], length)) {
            fprintf(stderr, "readonly: '%s': not a valid identifier\n", argv[i]);
            status = 1;
            continue;
        }
        memcpy(name, argv[i], length);
        name[length] = '\0';
        if (equals != NULL && var_set(name, equals + 1) != 0) {
            status = 1;
            continue;
        }
        var_set_readonly(name);
    }
    return status;
}

static int builtin_declare(char* const argv[]) {
    int i, status = 0, readonly = 0, exported = 0, print = 0, integer = 0, assoc = 0;

    for (i = 1; argv[i] != NULL && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        const char* f;

        for (f = argv[i] + 1; *f != '\0'; f++) {
            if (*f == 'r')
                readonly = 1;
            else if (*f == 'x')
                exported = 1;
            else if (*f == 'i')
                integer = 1;
            else if (*f == 'A')
                assoc = 1;
            else if (*f == 'p')
                print = 1;
        }
    }
    for (; argv[i] != NULL; i++) {
        const char* equals = strchr(argv[i], '=');
        char name[256];
        size_t length = equals != NULL ? (size_t)(equals - argv[i]) : strlen(argv[i]);

        if (length == 0 || length >= sizeof name || !valid_name(argv[i], length)) {
            fprintf(stderr, "declare: '%s': not a valid identifier\n", argv[i]);
            status = 1;
            continue;
        }
        memcpy(name, argv[i], length);
        name[length] = '\0';
        if (print) {
            const char* value = var_get(name);

            if (value == NULL) {
                fprintf(stderr, "declare: %s: not found\n", name);
                status = 1;
            } else {
                printf("declare -- %s=\"%s\"\n", name, value);
            }
            continue;
        }
        if (assoc && var_declare_assoc(name) != 0)
            status = 1;
        if (integer)
            var_set_integer(name);
        if (command_in_function()) {
            if (command_local(name, equals != NULL ? equals + 1 : NULL) != 0)
                status = 1;
        } else if (equals != NULL && var_set(name, equals + 1) != 0) {
            status = 1;
            continue;
        }
        if (exported)
            var_export(name, NULL);
        if (readonly)
            var_set_readonly(name);
    }
    return status;
}

/** What a command word is: 'f' function, 'b' builtin, 'p' program (path in out), 0 nothing. */
static int word_kind(const char* name, char* out, size_t size) {
    char native[1024];

    if (command_has_function(name))
        return 'f';
    if (builtin_exists(name))
        return 'b';
    if (program_find(name, native, sizeof native) == 0) {
        if (vpath_from_native(native, out, size) != 0)
            snprintf(out, size, "%s", native);
        return 'p';
    }
    return 0;
}

/** command -v / -V and type: tells what the names are. */
static int builtin_type(char* const argv[], int isType) {
    int i = 1, status = 0, terse = !isType, verbose = isType, onlyPath = 0;

    for (; argv[i] != NULL && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        const char* f;

        for (f = argv[i] + 1; *f != '\0'; f++) {
            if (*f == 'v') {
                terse = 1;
                verbose = 0;
            } else if (*f == 'V') {
                terse = 0;
                verbose = 1;
            } else if (*f == 't') {
                terse = 2;
                verbose = 0;
            } else if (*f == 'p' || *f == 'P') {
                onlyPath = 1;
            }
        }
    }
    for (; argv[i] != NULL; i++) {
        char path[1024];
        int kind = word_kind(argv[i], path, sizeof path);

        if (kind == 0 || (onlyPath && kind != 'p')) {
            if (verbose || (isType && terse != 2))
                fprintf(stderr, "%s: %s: not found\n", isType ? "type" : "command", argv[i]);
            status = 1;
        } else if (terse == 2) {
            puts(kind == 'f' ? "function" : (kind == 'b' ? "builtin" : "file"));
        } else if (verbose) {
            if (kind == 'f')
                printf("%s is a function\n", argv[i]);
            else if (kind == 'b')
                printf("%s is a shell builtin\n", argv[i]);
            else
                printf("%s is %s\n", argv[i], path);
        } else {
            puts(kind == 'p' ? path : argv[i]);
        }
    }
    return status;
}

/* ---- shopt ---------------------------------------------------------------------------------------------- */

/** The options of bash's shopt. Those that change something here are dotglob and nullglob; the others are kept and shown only. */
static struct {
    const char* name;
    int         on;
} shoptTable[] = {
    { "autocd", 0 }, { "cdspell", 0 }, { "checkwinsize", 1 }, { "cmdhist", 1 }, { "dotglob", 0 }, { "execfail", 0 },
    { "expand_aliases", 1 }, { "extglob", 1 }, { "failglob", 0 }, { "globstar", 0 }, { "histappend", 0 }, { "hostcomplete", 1 },
    { "inherit_errexit", 0 }, { "lastpipe", 0 }, { "nocaseglob", 0 }, { "nocasematch", 0 }, { "nullglob", 0 },
    { "progcomp", 1 }, { "promptvars", 1 }, { "sourcepath", 1 }, { "xpg_echo", 0 },
};

static void shopt_apply(void) {
    int dotglob = 0, nullglob = 0;
    size_t i;

    for (i = 0; i < sizeof shoptTable / sizeof *shoptTable; i++) {
        if (strcmp(shoptTable[i].name, "dotglob") == 0)
            dotglob = shoptTable[i].on;
        else if (strcmp(shoptTable[i].name, "nullglob") == 0)
            nullglob = shoptTable[i].on;
    }
    winshell_glob_options(dotglob, nullglob);
}

/** shopt [-pqsu] [-o] [NAME...] */
static int builtin_shopt(char* const argv[]) {
    int set = 0, unset = 0, quiet = 0, print = 0;
    int i = 1, status = 0;
    size_t k;

    for (; argv[i] != NULL && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        const char* o;

        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        for (o = argv[i] + 1; *o != '\0'; o++) {
            switch (*o) {
                case 's': set = 1; break;
                case 'u': unset = 1; break;
                case 'q': quiet = 1; break;
                case 'p': print = 1; break;
                case 'o': break;
                default:
                    fprintf(stderr, "shopt: -%c: invalid option\n"
                                    "shopt: usage: shopt [-pqsu] [-o] [optname ...]\n", *o);
                    return 2;
            }
        }
    }
    if (set && unset) {
        fprintf(stderr, "shopt: cannot set and unset shell options simultaneously\n");
        return 1;
    }
    if (argv[i] == NULL) {
        // every option, or (-s, -u) the ones that are on, or off
        for (k = 0; k < sizeof shoptTable / sizeof *shoptTable; k++) {
            if ((set && !shoptTable[k].on) || (unset && shoptTable[k].on))
                continue;
            if (quiet)
                continue;
            if (print)
                printf("shopt -%c %s\n", shoptTable[k].on ? 's' : 'u', shoptTable[k].name);
            else
                printf("%-15s\t%s\n", shoptTable[k].name, shoptTable[k].on ? "on" : "off");
        }
        return 0;
    }
    for (; argv[i] != NULL; i++) {
        for (k = 0; k < sizeof shoptTable / sizeof *shoptTable; k++)
            if (strcmp(shoptTable[k].name, argv[i]) == 0)
                break;
        if (k == sizeof shoptTable / sizeof *shoptTable) {
            fprintf(stderr, "shopt: %s: invalid shell option name\n", argv[i]);
            status = 1;
            continue;
        }
        if (set) {
            shoptTable[k].on = 1;
        } else if (unset) {
            shoptTable[k].on = 0;
        } else {
            // a query: the status says whether the option is on
            if (!shoptTable[k].on)
                status = 1;
            if (!quiet) {
                if (print)
                    printf("shopt -%c %s\n", shoptTable[k].on ? 's' : 'u', shoptTable[k].name);
                else
                    printf("%-15s\t%s\n", shoptTable[k].name, shoptTable[k].on ? "on" : "off");
            }
        }
    }
    shopt_apply();
    return status;
}

/** getopts OPTSTRING NAME [ARG...] */
static int builtin_getopts(char* const argv[]) {
    static long stateIndex = 0, stateOffset = 1;
    const char* optstring = argv[1];
    const char* name = argv[2];
    char* const* args;
    int count = 0, silent;
    long index;
    const char* current;
    char option;
    const char* at;
    char value[2];
    const char* indexText;

    if (optstring == NULL || name == NULL) {
        fprintf(stderr, "getopts: usage: getopts optstring name [arg ...]\n");
        return 2;
    }
    silent = optstring[0] == ':';
    if (argv[3] != NULL) {
        args = argv + 3;
        while (args[count] != NULL)
            count++;
    } else {
        // the parameters of the shell
        static char* positionalCopy[256];
        int n = winshell_positional_count(), k;

        for (k = 0; k < n && k < 255; k++)
            positionalCopy[k] = (char*)winshell_positional(k + 1);
        positionalCopy[k] = NULL;
        count = k;
        args = positionalCopy;
    }
    indexText = var_get("OPTIND");
    index = indexText != NULL ? atol(indexText) : 1;
    if (index < 1)
        index = 1;
    if (index != stateIndex)
        stateOffset = 1;
    stateIndex = index;

    if (index > count || args[index - 1] == NULL || args[index - 1][0] != '-' || args[index - 1][1] == '\0') {
        var_set(name, "?");
        return 1;
    }
    current = args[index - 1];
    if (strcmp(current, "--") == 0) {
        char number[32];

        snprintf(number, sizeof number, "%ld", index + 1);
        var_set("OPTIND", number);
        var_set(name, "?");
        stateIndex = 0;
        return 1;
    }
    option = current[stateOffset];
    value[0] = option;
    value[1] = '\0';
    at = option != ':' ? strchr(optstring + (silent ? 1 : 0), option) : NULL;
    {
        char number[32];
        long newIndex = index;
        int moreInWord = current[stateOffset + 1] != '\0';

        if (at == NULL) {
            var_set(name, "?");
            if (silent) {
                var_set("OPTARG", value);
            } else {
                fprintf(stderr, "getopts: illegal option -- %c\n", option);
                var_unset("OPTARG");
            }
        } else if (at[1] == ':') {
            if (moreInWord) {
                var_set(name, value);
                var_set("OPTARG", current + stateOffset + 1);
            } else if (index < count) {
                var_set(name, value);
                var_set("OPTARG", args[index]);
                newIndex = index + 1;
            } else if (silent) {
                var_set(name, ":");
                var_set("OPTARG", value);
            } else {
                var_set(name, "?");
                fprintf(stderr, "getopts: option requires an argument -- %c\n", option);
                var_unset("OPTARG");
            }
            moreInWord = 0;                     // the rest of the word was the argument
        } else {
            var_set(name, value);
            var_unset("OPTARG");
        }
        if (moreInWord) {
            stateOffset++;                      // more options in this word
        } else {
            stateOffset = 1;
            newIndex++;
        }
        snprintf(number, sizeof number, "%ld", newIndex);
        var_set("OPTIND", number);
        stateIndex = newIndex;
    }
    return 0;
}

/* ---- mapfile, printf -v, umask, ulimit ------------------------------------------------------------------ */

/** mapfile [-t] [-n COUNT] [-s SKIP] [-O ORIGIN] [NAME]: the lines of the input go to the array NAME (MAPFILE). */
static int builtin_mapfile(char* const argv[]) {
    int trim = 0, i;
    long maxCount = 0, skip = 0, origin = 0, count = 0, lineNumber = 0;
    const char* name = "MAPFILE";
    char** items = NULL;
    size_t used = 0, capacity = 0, k;
    char* line = NULL;

    for (i = 1; argv[i] != NULL && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        const char* o = argv[i] + 1;

        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        for (; *o != '\0'; o++) {
            if (*o == 't') {
                trim = 1;
            } else if (*o == 'n' || *o == 's' || *o == 'O' || *o == 'd' || *o == 'u' || *o == 'C' || *o == 'c') {
                const char* value = o[1] != '\0' ? o + 1 : argv[++i];

                if (value == NULL) {
                    fprintf(stderr, "mapfile: -%c: option requires an argument\n", *o);
                    return 2;
                }
                if (*o == 'n')
                    maxCount = atol(value);
                else if (*o == 's')
                    skip = atol(value);
                else if (*o == 'O')
                    origin = atol(value);
                else if (*o == 'd' && value[0] != '\0' && value[0] != '\n') {
                    fprintf(stderr, "mapfile: -d: only a line feed is supported\n");
                    return 2;
                }
                break;
            } else {
                fprintf(stderr, "mapfile: -%c: invalid option\n", *o);
                return 2;
            }
        }
    }
    if (argv[i] != NULL)
        name = argv[i];
    while (interp_read_line(&line)) {
        size_t n = strlen(line);

        lineNumber++;
        if (lineNumber <= skip) {
            free(line);
            continue;
        }
        if (VEC_RESERVE(items, capacity, used + 1) != 0) {
            free(line);
            return 1;
        }
        if (!trim) {
            char* with = malloc(n + 2);

            if (with == NULL) {
                free(line);
                return 1;
            }
            memcpy(with, line, n);
            with[n] = '\n';
            with[n + 1] = '\0';
            free(line);
            line = with;
        }
        items[used++] = line;
        line = NULL;
        if (maxCount > 0 && ++count >= maxCount)
            break;
    }
    if (used == 0) {
        var_array_assign(name, NULL, 0);
    } else if (origin == 0) {
        var_array_assign(name, items, used);
    } else {
        for (k = 0; k < used; k++)
            var_array_set(name, origin + (long)k, items[k]);
    }
    for (k = 0; k < used; k++)
        free(items[k]);
    free(items);
    return 0;
}

/** printf -v NAME FORMAT [ARG...]: the output of printf goes to the variable. */
static int builtin_printf_v(char* const argv[]) {
    char* command;
    char* output = NULL;
    size_t length = 0, i, total = 16;
    int status = 0;

    if (argv[2] == NULL) {
        fprintf(stderr, "printf: -v: option requires an argument\n");
        return 2;
    }
    for (i = 3; argv[i] != NULL; i++)
        total += strlen(argv[i]) * 4 + 4;
    command = malloc(total);
    if (command == NULL)
        return 1;
    strcpy(command, "command printf");
    for (i = 3; argv[i] != NULL; i++) {
        char* quoted = quote_word(argv[i]);

        if (quoted == NULL) {
            free(command);
            return 1;
        }
        strcat(command, " ");
        strcat(command, quoted);
        free(quoted);
    }
    if (interp_capture(command, &output, &length, &status) != 0) {
        free(command);
        return 1;
    }
    free(command);
    {
        char* value = malloc(length + 1);

        if (value == NULL) {
            free(output);
            return 1;
        }
        memcpy(value, output != NULL ? output : "", length);
        value[length] = '\0';
        var_set(argv[2], value);
        free(value);
    }
    free(output);
    return status;
}

/** umask [-p] [-S] [MODE]: Windows has no file creation mask; it is kept and shown, nothing more. */
static int builtin_umask(char* const argv[]) {
    static unsigned mask = 022;
    int symbolic = 0, print = 0, i = 1;

    for (; argv[i] != NULL && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        if (strcmp(argv[i], "-S") == 0) {
            symbolic = 1;
        } else if (strcmp(argv[i], "-p") == 0) {
            print = 1;
        } else if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        } else {
            fprintf(stderr, "umask: %s: invalid option\n", argv[i]);
            return 2;
        }
    }
    if (argv[i] != NULL) {
        char* end;
        unsigned long value = strtoul(argv[i], &end, 8);

        if (*end != '\0' || argv[i][0] == '\0' || value > 0777) {
            fprintf(stderr, "umask: %s: invalid octal number\n", argv[i]);
            return 1;
        }
        mask = (unsigned)value;
        return 0;
    }
    if (symbolic) {
        unsigned allowed = ~mask & 0777;

        printf("%su=%s%s%s,g=%s%s%s,o=%s%s%s\n", print ? "umask -S " : "",
               allowed & 0400 ? "r" : "", allowed & 0200 ? "w" : "", allowed & 0100 ? "x" : "",
               allowed & 040 ? "r" : "", allowed & 020 ? "w" : "", allowed & 010 ? "x" : "",
               allowed & 04 ? "r" : "", allowed & 02 ? "w" : "", allowed & 01 ? "x" : "");
    } else {
        printf("%s%04o\n", print ? "umask " : "", mask);
    }
    return 0;
}

/** ulimit [-SH] [-a] [-c -d -e -f -i -l -m -n -p -q -r -s -t -u -v -x] [LIMIT]: the limits that a UNIX shell shows; none applies. */
static int builtin_ulimit(char* const argv[]) {
    static const struct {
        char        letter;
        const char* description;
        const char* value;
    } table[] = {
        { 'c', "core file size          (blocks, -c)", "0" },
        { 'd', "data seg size           (kbytes, -d)", "unlimited" },
        { 'e', "scheduling priority             (-e)", "0" },
        { 'f', "file size               (blocks, -f)", "unlimited" },
        { 'i', "pending signals                 (-i)", "63000" },
        { 'l', "max locked memory       (kbytes, -l)", "65536" },
        { 'm', "max memory size         (kbytes, -m)", "unlimited" },
        { 'n', "open files                      (-n)", "1024" },
        { 'p', "pipe size            (512 bytes, -p)", "8" },
        { 'q', "POSIX message queues     (bytes, -q)", "819200" },
        { 'r', "real-time priority              (-r)", "0" },
        { 's', "stack size              (kbytes, -s)", "8192" },
        { 't', "cpu time               (seconds, -t)", "unlimited" },
        { 'u', "max user processes              (-u)", "63000" },
        { 'v', "virtual memory          (kbytes, -v)", "unlimited" },
        { 'x', "file locks                      (-x)", "unlimited" },
    };
    int all = 0, i = 1;
    char letter = 'f';
    size_t k;

    for (; argv[i] != NULL && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        const char* o = argv[i] + 1;

        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        for (; *o != '\0'; o++) {
            if (*o == 'S' || *o == 'H') {
                continue;
            } else if (*o == 'a') {
                all = 1;
            } else if (strchr("cdefilmnpqrstuvx", *o) != NULL) {
                letter = *o;
            } else {
                fprintf(stderr, "ulimit: -%c: invalid option\n", *o);
                return 2;
            }
        }
    }
    if (argv[i] != NULL)
        return 0;                       // setting a limit: accepted, nothing changes
    for (k = 0; k < sizeof table / sizeof table[0]; k++) {
        if (all)
            printf("%s %s\n", table[k].description, table[k].value);
        else if (table[k].letter == letter)
            printf("%s\n", table[k].value);
    }
    return 0;
}

/** Whether the command is one that the shell runs itself: a builtin by name, or printf -v. */
int builtin_wants(char* const argv[]) {
    if (argv[0] == NULL)
        return 0;
    if (builtin_exists(argv[0]))
        return 1;
    return strcmp(argv[0], "printf") == 0 && argv[1] != NULL && strcmp(argv[1], "-v") == 0;
}

int builtin_exists(const char* name) {
    static const char* const names[] = { "mapfile", "readarray", "builtin", "umask", "ulimit", "cd", "export", "unset", "exit", "quit", "source", ".", ":", "break",
                                         "continue", "read", "return", "local", "trap", "jobs", "wait", "shift", "fg", "bg", "disown", "set", "eval", "let", "readonly", "declare", "typeset", "type", "command", "getopts", "shopt", "exec" };
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
    if (strcmp(name, "mapfile") == 0 || strcmp(name, "readarray") == 0) {
        *status = builtin_mapfile(command->argv);
        return 1;
    }
    if (strcmp(name, "printf") == 0 && command->argv[1] != NULL && strcmp(command->argv[1], "-v") == 0) {
        *status = builtin_printf_v(command->argv);
        return 1;
    }
    if (strcmp(name, "umask") == 0) {
        *status = builtin_umask(command->argv);
        return 1;
    }
    if (strcmp(name, "ulimit") == 0) {
        *status = builtin_ulimit(command->argv);
        return 1;
    }
    if (strcmp(name, "builtin") == 0) {
        // builtin NAME ARG...: NAME is not looked for among the functions (bash builtins that are programs here run as programs)
        Command shifted = *command;
        char** argv;
        size_t n = 0, i;

        if (command->argv[1] == NULL) {
            *status = 0;
            return 1;
        }
        while (command->argv[n] != NULL)
            n++;
        argv = calloc(n + 1, sizeof *argv);
        if (argv == NULL) {
            *status = 1;
            return 1;
        }
        argv[0] = "command";
        for (i = 1; i < n; i++)
            argv[i] = command->argv[i];
        shifted.argv = argv;
        builtin_run(&shifted, status, exitShell);
        free(argv);
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
        int i, failed = 0, assoc = 0, integer = 0, readonly = 0;

        for (i = 1; command->argv[i] != NULL; i++) {
            const char* equals = strchr(command->argv[i], '=');

            if (command->argv[i][0] == '-' && command->argv[i][1] != '\0' && equals == NULL) {
                // local -a -A -i -r -x -n NAME: the flags that mean something here are kept for the names after them
                const char* f;

                for (f = command->argv[i] + 1; *f != '\0'; f++) {
                    if (*f == 'A')
                        assoc = 1;
                    else if (*f == 'i')
                        integer = 1;
                    else if (*f == 'r')
                        readonly = 1;
                }
                continue;
            }
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
            if (assoc)
                var_declare_assoc(variable);
            if (integer)
                var_set_integer(variable);
            if (readonly)
                var_set_readonly(variable);
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
    if (strcmp(name, "eval") == 0) {
        *status = builtin_eval(command->argv, status, exitShell);
        return 1;
    }
    if (strcmp(name, "let") == 0) {
        *status = builtin_let(command->argv);
        return 1;
    }
    if (strcmp(name, "readonly") == 0) {
        *status = builtin_readonly(command->argv);
        return 1;
    }
    if (strcmp(name, "declare") == 0 || strcmp(name, "typeset") == 0) {
        *status = builtin_declare(command->argv);
        return 1;
    }
    if (strcmp(name, "type") == 0) {
        *status = builtin_type(command->argv, 1);
        return 1;
    }
    if (strcmp(name, "getopts") == 0) {
        *status = builtin_getopts(command->argv);
        return 1;
    }
    if (strcmp(name, "exec") == 0) {
        // exec COMMAND ARG...: the command takes the place of the shell: it runs, and the shell ends with its status
        char* const* argv = command->argv;
        size_t i, length = 1;
        char* text;

        if (argv[1] == NULL) {
            *status = 0;
            return 1;
        }
        for (i = 1; argv[i] != NULL; i++) {
            char* quoted = quote_word(argv[i]);

            if (quoted == NULL) {
                *status = 1;
                return 1;
            }
            length += strlen(quoted) + 1;
            free(quoted);
        }
        text = malloc(length);
        if (text == NULL) {
            *status = 1;
            return 1;
        }
        text[0] = 0;
        for (i = 1; argv[i] != NULL; i++) {
            char* quoted = quote_word(argv[i]);

            if (i > 1)
                strcat(text, " ");
            strcat(text, quoted);
            free(quoted);
        }
        command_bypass_functions(1);
        *status = interp_run_line(text, exitShell, *status);
        command_bypass_functions(0);
        free(text);
        *exitShell = 1;
        return 1;
    }
    if (strcmp(name, "shopt") == 0) {
        *status = builtin_shopt(command->argv);
        return 1;
    }
    if (strcmp(name, "command") == 0) {
        char* const* argv = command->argv;

        if (argv[1] != NULL && (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "-p") == 0 && argv[2] != NULL && (strcmp(argv[2], "-v") == 0 || strcmp(argv[2], "-V") == 0))) {
            *status = builtin_type(argv, 0);
        } else {
            // command NAME ARG...: NAME is not looked for among the functions
            int first = 1;
            size_t i, length = 1;
            char* text;
            int result;

            if (argv[first] != NULL && strcmp(argv[first], "-p") == 0)
                first++;
            if (argv[first] != NULL && strcmp(argv[first], "--") == 0)
                first++;
            if (argv[first] == NULL) {
                *status = 0;
                return 1;
            }
            for (i = (size_t)first; argv[i] != NULL; i++) {
                char* quoted = quote_word(argv[i]);

                if (quoted == NULL) {
                    *status = 1;
                    return 1;
                }
                length += strlen(quoted) + 1;
                free(quoted);
            }
            text = malloc(length);
            if (text == NULL) {
                *status = 1;
                return 1;
            }
            text[0] = '\0';
            for (i = (size_t)first; argv[i] != NULL; i++) {
                char* quoted = quote_word(argv[i]);

                if (i > (size_t)first)
                    strcat(text, " ");
                strcat(text, quoted);
                free(quoted);
            }
            command_bypass_functions(1);
            result = interp_run_line(text, exitShell, *status);
            command_bypass_functions(0);
            free(text);
            *status = result;
        }
        return 1;
    }
    if (strcmp(name, "set") == 0) {
        *status = command_set(command->argv);
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
