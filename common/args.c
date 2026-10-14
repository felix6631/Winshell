#include "args.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void try_help(const ArgsSpec* spec) {
    fprintf(stderr, "Try '%s --help' for more information.\n", spec->program);
}

static const ArgsOption* find_short(const ArgsSpec* spec, char c) {
    const ArgsOption* o;
    for (o = spec->options; o->id != 0; o++)
        if (o->shortName == c)
            return o;
    return NULL;
}

/** The long option named name[0..length), exactly or by a prefix that only one option has. */
static const ArgsOption* find_long(const ArgsSpec* spec, const char* name, size_t length, int* ambiguous) {
    const ArgsOption *o, *found = NULL;

    *ambiguous = 0;
    for (o = spec->options; o->id != 0; o++) {
        if (o->longName == NULL || strncmp(o->longName, name, length) != 0)
            continue;
        if (o->longName[length] == '\0')
            return o;                   // exact
        if (found != NULL && found->id != o->id)
            *ambiguous = 1;
        found = o;
    }
    return *ambiguous ? NULL : found;
}

/**
 * A long option, with the argument at *a: --name, --name=value, a prefix of a name, or --name value.
 * Returns ARGS_OK, ARGS_EXIT (--help, --version) or ARGS_FAILED; *a moves on past a value taken from the next word.
 */
static int parse_long(const ArgsSpec* spec, int argc, char* argv[], int* a, ArgsHandler handler, void* context) {
    const char* arg = argv[*a];
    const char* equals = strchr(arg + 2, '=');
    size_t nameLength = equals != NULL ? (size_t)(equals - (arg + 2)) : strlen(arg + 2);
    const char* value = equals != NULL ? equals + 1 : NULL;
    const ArgsOption* option;
    int ambiguous;

    if (strcmp(arg, "--help") == 0) {
        spec->usage();
        return ARGS_EXIT;
    }
    if (strcmp(arg, "--version") == 0) {
        printf("%s (Winshell) %s\n", spec->program, spec->version);
        return ARGS_EXIT;
    }

    option = find_long(spec, arg + 2, nameLength, &ambiguous);
    if (option == NULL) {
        if (ambiguous)
            fprintf(stderr, "%s: option '%.*s' is ambiguous\n", spec->program, (int)(nameLength + 2), arg);
        else
            fprintf(stderr, "%s: unrecognized option '%.*s'\n", spec->program, (int)(nameLength + 2), arg);
        try_help(spec);
        return ARGS_FAILED;
    }

    if (option->argument == ARGS_NONE && value != NULL) {
        fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n", spec->program, option->longName);
        try_help(spec);
        return ARGS_FAILED;
    }
    if (option->argument == ARGS_REQUIRED && value == NULL) {
        if (*a + 1 >= argc) {
            fprintf(stderr, "%s: option '--%s' requires an argument\n", spec->program, option->longName);
            try_help(spec);
            return ARGS_FAILED;
        }
        value = argv[++*a];
    }
    return handler(option->id, value, context) != 0 ? ARGS_FAILED : ARGS_OK;
}

/** Grouped short options; the first one with a value takes the rest of the word or the next word. */
static int parse_short(const ArgsSpec* spec, int argc, char* argv[], int* a, ArgsHandler handler, void* context) {
    const char* p;

    for (p = argv[*a] + 1; *p != '\0'; p++) {
        const ArgsOption* option = find_short(spec, *p);
        const char* value = NULL;

        if (option == NULL) {
            fprintf(stderr, "%s: invalid option -- '%c'\n", spec->program, *p);
            try_help(spec);
            return ARGS_FAILED;
        }
        if (option->argument == ARGS_REQUIRED) {
            if (p[1] != '\0') {
                value = p + 1;
            } else if (*a + 1 < argc) {
                value = argv[++*a];
            } else {
                fprintf(stderr, "%s: option requires an argument -- '%c'\n", spec->program, *p);
                try_help(spec);
                return ARGS_FAILED;
            }
        }
        if (option->argument == ARGS_OPTIONAL && p[1] != '\0')
            value = p + 1;      // only attached to the option: -i.bak
        if (handler(option->id, value, context) != 0)
            return ARGS_FAILED;
        if (value != NULL)
            break;
    }
    return ARGS_OK;
}

/** -5: a number as an option. */
static int is_number_option(const ArgsSpec* spec, const char* arg) {
    const char* p;

    if (!spec->numberOption || arg[1] < '0' || arg[1] > '9')
        return 0;
    for (p = arg + 1; *p >= '0' && *p <= '9'; p++)
        ;
    return *p == '\0';
}

int args_parse(const ArgsSpec* spec, int argc, char* argv[], ArgsHandler handler, void* context,
               const char*** operandsOut, int* operandCountOut) {
    const char** operands;
    int count = 0, optionsEnded = 0, a;

    operands = calloc((size_t)argc + 1, sizeof *operands);
    if (operands == NULL) {
        fprintf(stderr, "%s: out of memory\n", spec->program);
        return ARGS_FAILED;
    }

    for (a = 1; a < argc; a++) {
        const char* arg = argv[a];
        int result;

        if (optionsEnded || arg[0] != '-' || arg[1] == '\0') {
            if (spec->stopAtOperand)
                optionsEnded = 1;
            operands[count++] = arg;
            continue;
        }

        if (strcmp(arg, "--") == 0) {
            optionsEnded = 1;
            continue;
        }

        if (arg[1] == '-')
            result = parse_long(spec, argc, argv, &a, handler, context);
        else if (is_number_option(spec, arg))
            result = handler(ARGS_NUMBER, arg + 1, context) != 0 ? ARGS_FAILED : ARGS_OK;
        else
            result = parse_short(spec, argc, argv, &a, handler, context);
        if (result != ARGS_OK) {
            free(operands);
            return result;
        }
    }

    *operandsOut = operands;
    *operandCountOut = count;
    return ARGS_OK;
}
