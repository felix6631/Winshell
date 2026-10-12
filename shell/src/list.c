/*
 * Cutting a command line into the pipelines of a list: at ;  &&  and  ||  that are
 * outside of quotes. See parser.h.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "parser.h"

static int is_blank(CHAR c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* ---- lists of pipelines ------------------------------------------------------------ */

static const char* list_operator_name(int op) {
    return op == LIST_AND ? "&&" : (op == LIST_OR ? "||" : ";");
}

static int is_blank_text(const char* start, size_t length) {
    size_t i;
    for (i = 0; i < length; i++)
        if (!is_blank(start[i]))
            return 0;
    return 1;
}

static int list_add(CommandList* list, const char* start, size_t length, int op) {
    ListItem* grown = realloc(list->items, (list->count + 1) * sizeof *grown);
    char* copy;

    if (grown == NULL)
        return -1;
    list->items = grown;
    copy = malloc(length + 1);
    if (copy == NULL)
        return -1;
    memcpy(copy, start, length);
    copy[length] = '\0';
    list->items[list->count].text = copy;
    list->items[list->count].op = op;
    list->count++;
    return 0;
}

int winshell_parse_list(const CHAR* line, CommandList* list) {
    size_t i = 0, segmentStart = 0;
    CHAR quote = '\0';
    int pending = LIST_FIRST;

    memset(list, 0, sizeof *list);

    for (;; i++) {
        CHAR c = line[i];
        int op = 0;
        size_t operatorLength = 0;

        if (c != '\0' && quote == '\'') {
            if (c == '\'')
                quote = '\0';
            continue;
        }

        if (c == '\\') {
            // backslashes in front of a double quote: an odd number escapes it
            size_t backslashes = 0;
            while (line[i + backslashes] == '\\')
                backslashes++;
            if (line[i + backslashes] == '"') {
                if (backslashes % 2 == 1)
                    i += backslashes;           // the quote is escaped: skip it as well
                else
                    i += backslashes - 1;       // the quote that follows toggles
            } else {
                i += backslashes - 1;
            }
            continue;
        }

        if (c == '"') {
            quote = quote == '"' ? '\0' : '"';
            continue;
        }
        if (c == '\'' && quote == '\0') {
            quote = '\'';
            continue;
        }

        // inside double quotes nothing is an operator
        if (c != '\0' && quote != '\0')
            continue;

        if (c != '\0' && quote == '\0') {
            if (c == ';') {
                op = LIST_SEQ;
                operatorLength = 1;
            } else if (c == '&' && line[i + 1] == '&') {
                op = LIST_AND;
                operatorLength = 2;
            } else if (c == '|' && line[i + 1] == '|') {
                op = LIST_OR;
                operatorLength = 2;
            } else if (c == '&' && (i > 0 && line[i - 1] == '>')) {
                continue;       // 2>&1 and the like belong to the segment
            } else if (c == '&' && line[i + 1] == '>') {
                i++;            // &> and &>>
                continue;
            } else if (c == '&') {
                fprintf(stderr, "Error: Syntax error: '&' is not supported (no background commands)\n");
                goto err;
            } else if (c == '|') {
                continue;       // a pipe belongs to the segment
            } else {
                continue;
            }
        }

        if (c == '\0') {
            // the end: the last segment, which may be empty after a ';' or in an empty line
            if (is_blank_text(line + segmentStart, i - segmentStart)) {
                if (pending == LIST_AND || pending == LIST_OR) {
                    fprintf(stderr, "Error: Syntax error: missing command after '%s'\n", list_operator_name(pending));
                    goto err;
                }
                break;
            }
            if (list_add(list, line + segmentStart, i - segmentStart, pending) != 0)
                goto err_memory;
            break;
        }

        // an operator ends a segment, which has to have a command in it
        if (is_blank_text(line + segmentStart, i - segmentStart)) {
            fprintf(stderr, "Error: Syntax error near unexpected token '%s'\n", list_operator_name(op));
            goto err;
        }
        if (list_add(list, line + segmentStart, i - segmentStart, pending) != 0)
            goto err_memory;
        pending = op;
        i += operatorLength - 1;
        segmentStart = i + 1;
    }
    return 0;

err_memory:
    fprintf(stderr, "Error: Out of memory\n");
err:
    winshell_freeList(list);
    return -1;
}

void winshell_freeList(CommandList* list) {
    size_t i;
    for (i = 0; i < list->count; i++)
        free(list->items[i].text);
    free(list->items);
    memset(list, 0, sizeof *list);
}
