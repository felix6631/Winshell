/*
 * Cutting a command line into the pipelines of a list: at ;  &&  and  ||  that are
 * outside of quotes. See parser.h.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "parser.h"
#include "vec.h"

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
    char* copy;

    if (VEC_GROW_BY_COUNT(list->items, list->count) != 0)
        return -1;
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

/**
 * Passes what no operator can be in: a quote mark, a run of backslashes (an odd number in front of a
 * double quote escapes it), and any character inside quotes. Returns how many characters were
 * passed, or 0 when line[i] is outside of quotes (it may be an operator) or the end of the line.
 */
static size_t pass_quoted(const CHAR* line, size_t i, CHAR* quote) {
    CHAR c = line[i];
    size_t backslashes = 0;

    if (c == '\0')
        return 0;

    if (*quote == '\'') {
        if (c == '\'')
            *quote = '\0';
        return 1;
    }

    if (c == '\\') {
        while (line[i + backslashes] == '\\')
            backslashes++;
        if (line[i + backslashes] == '"' && backslashes % 2 == 1)
            return backslashes + 1;     // the quote is escaped: pass it as well
        return backslashes;             // an even number: the quote that follows toggles
    }

    if (c == '"') {
        *quote = *quote == '"' ? '\0' : '"';
        return 1;
    }
    if (c == '\'' && *quote == '\0') {
        *quote = '\'';
        return 1;
    }
    return *quote != '\0' ? 1 : 0;      // inside double quotes nothing is an operator
}

/**
 * The list operator at line[i]: LIST_SEQ, LIST_AND or LIST_OR with its *length, or 0 for a
 * character that belongs to the pipeline (*skip says how many more characters do, as for the
 * second one of "&>"). -1 for a '&' that starts no operator of this shell (no background jobs).
 */
static int operator_at(const CHAR* line, size_t i, size_t* length, size_t* skip) {
    CHAR c = line[i];

    *length = *skip = 0;
    if (c == ';') {
        *length = 1;
        return LIST_SEQ;
    }
    if (c == '&' && line[i + 1] == '&') {
        *length = 2;
        return LIST_AND;
    }
    if (c == '|' && line[i + 1] == '|') {
        *length = 2;
        return LIST_OR;
    }
    if (c == '&') {
        if (i > 0 && line[i - 1] == '>')
            return 0;                   // 2>&1 and the like
        if (line[i + 1] == '>') {
            *skip = 1;                  // &> and &>>
            return 0;
        }
        return -1;
    }
    return 0;                           // a pipe belongs to the segment, as do the ordinary characters
}

int winshell_parse_list(const CHAR* line, CommandList* list) {
    size_t i = 0, segmentStart = 0;
    CHAR quote = '\0';
    int pending = LIST_FIRST;

    memset(list, 0, sizeof *list);

    for (;; i++) {
        size_t passed, operatorLength, skip;
        int op;

        // $( ) and ` ` are one piece, whatever is inside of them (outside of single quotes)
        if (quote != '\'' && ((line[i] == '$' && line[i + 1] == '(') || line[i] == '`')) {
            size_t length = winshell_substitution_length(line + i);
            if (length > 0) {
                i += length - 1;
                continue;
            }
        }

        passed = pass_quoted(line, i, &quote);
        if (passed > 0) {
            i += passed - 1;
            continue;
        }

        // a # at the start of a word (outside of quotes) starts a comment: the line ends there
        if (line[i] == '#' && (i == 0 || is_blank(line[i - 1]) || strchr(";|&", line[i - 1]) != NULL)) {
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

        if (line[i] == '\0') {
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

        op = operator_at(line, i, &operatorLength, &skip);
        if (op < 0) {
            fprintf(stderr, "Error: Syntax error: '&' is not supported (no background commands)\n");
            goto err;
        }
        if (op == 0) {
            i += skip;
            continue;
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
