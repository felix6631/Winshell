#include "cmdline.h"
#include <stdlib.h>
#include <string.h>

/**
 * Writes arg into out, quoted following the rules of CommandLineToArgvW so
 * the child gets the very same argument back. Returns the end of the output.
 */
static char* quote_arg(char* out, const char* arg) {
    size_t backslashes, i;

    if (*arg != '\0' && strpbrk(arg, " \t\"") == NULL) {
        size_t length = strlen(arg);
        memcpy(out, arg, length);
        return out + length;
    }

    *out++ = '"';
    for (;;) {
        backslashes = 0;
        while (*arg == '\\') {
            arg++;
            backslashes++;
        }

        if (*arg == '\0') {
            // backslashes in front of the closing quote must be doubled
            for (i = 0; i < backslashes * 2; i++)
                *out++ = '\\';
            break;
        }

        if (*arg == '"') {
            // backslashes in front of a quote must be doubled, and the quote escaped
            for (i = 0; i < backslashes * 2 + 1; i++)
                *out++ = '\\';
        } else {
            for (i = 0; i < backslashes; i++)
                *out++ = '\\';
        }
        *out++ = *arg++;
    }
    *out++ = '"';
    return out;
}

char* cmdline_build(char* const argv[]) {
    size_t size = 1, i;
    char *command, *end;

    for (i = 0; argv[i] != NULL; i++)
        size += strlen(argv[i]) * 2 + 3;

    command = malloc(size);
    if (command == NULL)
        return NULL;

    end = command;
    for (i = 0; argv[i] != NULL; i++) {
        if (i > 0)
            *end++ = ' ';
        end = quote_arg(end, argv[i]);
    }
    *end = '\0';
    return command;
}
