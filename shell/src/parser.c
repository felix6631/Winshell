#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "parser.h"

#define DELIMITERS " \t\r\n"

static int is_delimiter(char c) {
    return strchr(DELIMITERS, c) != NULL;
}

LPSTR* winshell_parser(CHAR* cmdline) {
    size_t length = 0, capacity = 8;
    LPSTR* parsed = malloc(capacity * sizeof *parsed);
    // Words are written back over the line itself. A word is never longer
    // than the text it comes from, so the write cursor never passes the read cursor.
    CHAR *read = cmdline, *write = cmdline;

    if (parsed == NULL) {
        fprintf(stderr, "Error: Out of memory\n");
        return NULL;
    }

    for (;;) {
        CHAR quote = '\0';  // the quote character we are inside of, if any
        LPSTR word;

        while (*read != '\0' && is_delimiter(*read))
            read++;
        if (*read == '\0')
            break;

        word = write;
        while (*read != '\0') {
            CHAR c = *read;

            if (quote == '\'') {
                // single quotes keep everything as it is
                if (c == '\'')
                    quote = '\0';
                else
                    *write++ = c;
                read++;
            } else if (c == '\\') {
                // a backslash is only special in front of a double quote:
                // 2n backslashes give n backslashes, 2n+1 give n and a literal quote
                size_t count = 0, i;
                while (read[count] == '\\')
                    count++;

                if (read[count] == '"') {
                    for (i = 0; i < count / 2; i++)
                        *write++ = '\\';
                    read += count;
                    if (count % 2 == 1)
                        *write++ = *read++;
                } else {
                    for (i = 0; i < count; i++)
                        *write++ = '\\';
                    read += count;
                }
            } else if (c == '"') {
                quote = (quote == '"') ? '\0' : '"';
                read++;
            } else if (c == '\'' && quote == '\0') {
                quote = '\'';
                read++;
            } else if (quote == '\0' && is_delimiter(c)) {
                break;
            } else {
                *write++ = c;
                read++;
            }
        }

        if (quote != '\0') {
            fprintf(stderr, "Error: Unterminated %s quote\n", quote == '"' ? "double" : "single");
            free(parsed);
            return NULL;
        }

        // keep room for this word and the terminating NULL
        if (length + 2 > capacity) {
            LPSTR* grown;
            capacity *= 2;
            grown = realloc(parsed, capacity * sizeof *parsed);
            if (grown == NULL) {
                fprintf(stderr, "Error: Out of memory\n");
                free(parsed);
                return NULL;
            }
            parsed = grown;
        }
        parsed[length++] = word;

        // terminate the word; this may overwrite the delimiter at *read, so step over it first
        if (*read != '\0') {
            *write++ = '\0';
            read++;
        } else {
            *write = '\0';
            break;
        }
    }

    parsed[length] = NULL;
    return parsed;
}
