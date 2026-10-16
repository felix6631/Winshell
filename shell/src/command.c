/*
 * Lists and compound commands: the parser (command text to a tree, one top-level command at a time) and
 * the code that runs the tree. A simple command stays text until its turn comes and goes to the pipeline
 * parser then, so variables and substitutions see what the commands before it did.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "builtin.h"
#include "jobs.h"
#include "command.h"
#include "cwd.h"
#include "executer.h"
#include "interp.h"
#include "parser.h"
#include "variables.h"
#include "vec.h"
#include "vpath.h"

/* ---- the tree ---------------------------------------------------------------------------- */

typedef struct Node Node;

typedef struct {
    Node** nodes;
    int*   ops;                 // LIST_FIRST for the first, then LIST_SEQ, LIST_AND, LIST_OR
    size_t count;
} List;

typedef enum {
    NODE_SIMPLE, NODE_IF, NODE_WHILE, NODE_FOR, NODE_PIPE, NODE_CASE, NODE_GROUP, NODE_SUBSHELL, NODE_FUNCTION,
    NODE_BACKGROUND             // list &: first is the and-or list, text its source
} NodeType;

/** One clause of a case: the patterns (text, expanded when the case is run) and the commands. */
typedef struct {
    char** patterns;
    size_t patternCount;
    List   body;
} CaseClause;

struct Node {
    CaseClause* clauses;        // NODE_CASE; its word is in text
    size_t   clauseCount;
    NodeType type;
    char*    text;              // NODE_SIMPLE: the command; NODE_FOR: the words after `in` (NULL: "$@")
    char*    redirects;         // a compound command: the redirections written after it (`done < file`), or NULL
    char*    name;              // NODE_FOR: the variable; NODE_FUNCTION: the name of the function
    int      until;             // NODE_WHILE: until
    int      hasElse;           // NODE_IF
    List     first;             // IF: the condition, WHILE: the condition
    List     second;            // IF: then, WHILE and FOR: the body
    List     third;             // IF: else (an `elif` is an IF in it)
};

static void free_node(Node* node);

static void free_list(List* list) {
    size_t i;

    for (i = 0; i < list->count; i++)
        free_node(list->nodes[i]);
    free(list->nodes);
    free(list->ops);
    memset(list, 0, sizeof *list);
}

static void free_node(Node* node) {
    if (node == NULL)
        return;
    size_t i, k;

    for (i = 0; i < node->clauseCount; i++) {
        for (k = 0; k < node->clauses[i].patternCount; k++)
            free(node->clauses[i].patterns[k]);
        free(node->clauses[i].patterns);
        free_list(&node->clauses[i].body);
    }
    free(node->clauses);
    free(node->text);
    free(node->redirects);
    free(node->name);
    free_list(&node->first);
    free_list(&node->second);
    free_list(&node->third);
    free(node);
}

static int list_add(List* list, Node* node, int op) {
    Node** nodes = realloc(list->nodes, (list->count + 1) * sizeof *nodes);
    int* ops;

    if (nodes == NULL)
        return -1;
    list->nodes = nodes;
    ops = realloc(list->ops, (list->count + 1) * sizeof *ops);
    if (ops == NULL)
        return -1;
    list->ops = ops;
    list->nodes[list->count] = node;
    list->ops[list->count] = op;
    list->count++;
    return 0;
}

/* ---- the parser -------------------------------------------------------------------------- */

enum { ERROR_NONE = 0, ERROR_INCOMPLETE, ERROR_SYNTAX, ERROR_MEMORY };

typedef struct {
    const char* text;
    size_t      pos;
    int         error;
    int         quiet;          // no messages: only the kind of error is wanted
    int         pipeToCompound; // scan_simple stopped at a | that is followed by a compound command
    int         stopAtPipe;     // scan_simple ends at a | (the redirections after a compound command)
    int         subshellDepth;  // inside ( ... ): a ) ends a command
    char        message[200];
} Parser;

static int is_blank(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static int is_word_end(char c) {
    return c == '\0' || is_blank(c) || c == '\n' || c == ';' || c == '&' || c == '|' || c == '<' || c == '>';
}

static int syntax(Parser* p, const char* format, const char* detail) {
    if (p->error == ERROR_NONE) {
        p->error = ERROR_SYNTAX;
        snprintf(p->message, sizeof p->message, format, detail != NULL ? detail : "");
    }
    return -1;
}

/** The text ends where a command has to go on: more input would complete it. */
static int incomplete(Parser* p, const char* message) {
    if (p->error == ERROR_NONE) {
        p->error = ERROR_INCOMPLETE;
        snprintf(p->message, sizeof p->message, "%s", message);
    }
    return -1;
}

/**
 * Skips blanks, newlines and comments between commands, and the ; that ends a command (afterCommand): one
 * of them, as a command has to be in front of it. Returns 0, or -1 for a ; with no command before it.
 */
static int skip_separators(Parser* p, int afterCommand) {
    int semicolonAllowed = afterCommand;

    for (;;) {
        char c = p->text[p->pos];

        if (c == ';' && p->text[p->pos + 1] == ';')
            return 0;                   // ;; ends the clause of a case: the caller looks for it
        if (c == ';') {
            if (!semicolonAllowed)
                return syntax(p, "Syntax error near unexpected token '%s'", ";");
            semicolonAllowed = 0;
            p->pos++;
        } else if (c == '\n') {
            semicolonAllowed = 0;
            p->pos++;
        } else if (is_blank(c)) {
            p->pos++;
        } else if (c == '#') {
            while (p->text[p->pos] != '\0' && p->text[p->pos] != '\n')
                p->pos++;
        } else {
            return 0;
        }
    }
}

static void skip_blanks(Parser* p) {
    while (is_blank(p->text[p->pos]))
        p->pos++;
}

/** The word at position pos, when it is a plain unquoted word (a candidate for a reserved word). */
static int plain_word(const Parser* p, size_t pos, char* out, size_t size, size_t* end) {
    size_t n = 0;

    while (!is_word_end(p->text[pos + n])) {
        char c = p->text[pos + n];

        if (c == '\'' || c == '"' || c == '\\' || c == '$' || c == '`' || c == '(' || c == ')' || c == '#' ||
            n + 1 >= size)
            return 0;
        out[n] = c;
        n++;
    }
    out[n] = '\0';
    *end = pos + n;
    return n > 0;
}

static int at_word(const Parser* p, const char* word) {
    char buffer[16];
    size_t end;

    return plain_word(p, p->pos, buffer, sizeof buffer, &end) && strcmp(buffer, word) == 0;
}

static int is_reserved(const char* word) {
    static const char* const words[] = { "if", "then", "elif", "else", "fi", "while", "until", "do", "done", "for", "in",
                                          "case", "esac", "{", "}" };
    size_t i;

    for (i = 0; i < sizeof words / sizeof words[0]; i++)
        if (strcmp(word, words[i]) == 0)
            return 1;
    return 0;
}

/** The reserved word at the position (unquoted, whole word), or NULL. */
static const char* reserved_here(const Parser* p, char* buffer, size_t size) {
    size_t end;

    if (plain_word(p, p->pos, buffer, size, &end) && is_reserved(buffer))
        return buffer;
    return NULL;
}

static int expect_word(Parser* p, const char* word) {
    char buffer[16];
    size_t end;

    if (skip_separators(p, 1) != 0)
        return -1;
    if (p->text[p->pos] == '\0') {
        char message[100];

        snprintf(message, sizeof message, "Syntax error: unexpected end of input (expected '%s')", word);
        return incomplete(p, message);
    }
    if (plain_word(p, p->pos, buffer, sizeof buffer, &end) && strcmp(buffer, word) == 0) {
        p->pos = end;
        return 0;
    }
    {
        size_t length = 0;
        char found[40];

        while (!is_word_end(p->text[p->pos + length]) && length < sizeof found - 1) {
            found[length] = p->text[p->pos + length];
            length++;
        }
        found[length] = '\0';
        p->error = ERROR_SYNTAX;
        snprintf(p->message, sizeof p->message, "Syntax error: unexpected '%s' where '%s' was expected", found, word);
        return -1;
    }
}

/**
 * Finds the end of a simple command that starts at pos: its text goes up to a ; newline && or || outside of
 * quotes and substitutions (a comment is passed). Returns the end, or (size_t)-1 after an error.
 */
static size_t scan_simple(Parser* p, size_t start) {
    const char* text = p->text;
    size_t i = start;
    char quote = '\0';

    for (;;) {
        char c = text[i];
        size_t passed, length, skip;
        int op;

        if (c == '\0') {
            if (quote != '\0')
                return incomplete(p, quote == '"' ? "Unterminated double quote" : "Unterminated single quote"),
                       (size_t)-1;
            return i;
        }
        if (quote != '\'' && ((c == '$' && (text[i + 1] == '(' || text[i + 1] == '{')) || c == '`')) {
            size_t n = winshell_substitution_length(text + i);

            if (n == 0)
                return incomplete(p, "Syntax error: unterminated command substitution"), (size_t)-1;
            i += n;
            continue;
        }
        passed = winshell_pass_quoted(text, i, &quote);
        if (passed > 0) {
            i += passed;
            continue;
        }
        if (c == '\n')
            return i;
        if (c == '#' && (i == start || is_blank(text[i - 1]) || strchr(";|&", text[i - 1]) != NULL)) {
            while (text[i] != '\0' && text[i] != '\n')
                i++;
            continue;
        }
        if (c == ')' && p->subshellDepth > 0 && quote == '\0')
            return i;
        if (c == '|' && text[i + 1] != '|' && p->stopAtPipe)
            return i;
        if (c == '|' && text[i + 1] != '|') {
            // a pipe into a compound command (`cmd | while read x; do ...; done`) ends the simple command
            size_t j = i + 1;
            char word[16];
            size_t wordEnd;

            while (is_blank(text[j]) || text[j] == '\n')
                j++;
            if (plain_word(p, j, word, sizeof word, &wordEnd) &&
                (strcmp(word, "while") == 0 || strcmp(word, "until") == 0 || strcmp(word, "for") == 0 ||
                 strcmp(word, "if") == 0 || strcmp(word, "case") == 0 || strcmp(word, "{") == 0)) {
                p->pipeToCompound = 1;
                return i;
            }
        }
        op = winshell_operator_at(text, i, &length, &skip);
        if (op < 0)
            return i;                   // a lone & (the command goes to the background): parse_andor takes it
        if (op > 0)
            return i;
        i += skip + 1;
    }
}

static Node* new_node(NodeType type) {
    return calloc(1, sizeof(Node));
}

static int parse_andor(Parser* p, List* list, int firstOp);
static int parse_list(Parser* p, List* list, const char* const* stops);
static Node* parse_command(Parser* p);
static Node* parse_single(Parser* p);

static int is_stop(const Parser* p, const char* const* stops) {
    char buffer[16];
    size_t end;

    if (stops == NULL || !plain_word(p, p->pos, buffer, sizeof buffer, &end))
        return 0;
    for (; *stops != NULL; stops++)
        if (strcmp(buffer, *stops) == 0)
            return 1;
    return 0;
}

/** Commands up to one of the stop words (not consumed) or the end of the text. */
static int parse_list(Parser* p, List* list, const char* const* stops) {
    int op = LIST_FIRST;

    for (;;) {
        if (skip_separators(p, op != LIST_FIRST) != 0)
            return -1;
        if (p->text[p->pos] == '\0' || is_stop(p, stops))
            return 0;
        if (p->text[p->pos] == ';' && p->text[p->pos + 1] == ';')
            return 0;                   // the end of a clause of a case
        if (p->text[p->pos] == ')' && p->subshellDepth > 0)
            return 0;
        if (parse_andor(p, list, op) != 0)
            return -1;
        op = LIST_SEQ;
    }
}

/** A command, then any number of `&& command` and `|| command`, to a ; newline or the end. */
static int parse_andor(Parser* p, List* list, int firstOp) {
    int op = firstOp;
    size_t firstIndex = list->count, sourceStart;

    skip_blanks(p);
    sourceStart = p->pos;
    for (;;) {
        Node* node;
        size_t length, skip;
        int next;

        node = parse_command(p);
        if (node == NULL)
            return -1;
        if (list_add(list, node, op) != 0) {
            free_node(node);
            p->error = ERROR_MEMORY;
            return -1;
        }
        skip_blanks(p);
        if (p->text[p->pos] == '#') {
            while (p->text[p->pos] != '\0' && p->text[p->pos] != '\n')
                p->pos++;
        }
        next = winshell_operator_at(p->text, p->pos, &length, &skip);
        if (next == LIST_AND || next == LIST_OR) {
            p->pos += length;
            op = next;
            // a command may be on the next line after && and ||
            for (;;) {
                skip_blanks(p);
                if (p->text[p->pos] == '\n')
                    p->pos++;
                else
                    break;
            }
            if (p->text[p->pos] == '\0')
                return incomplete(p, next == LIST_AND ? "Syntax error: missing command after '&&'"
                                                      : "Syntax error: missing command after '||'");
            continue;
        }
        if (p->text[p->pos] == '&' && p->text[p->pos + 1] != '&' && p->text[p->pos + 1] != '>') {
            // list &: the whole and-or list runs as a background job
            Node* job = new_node(NODE_BACKGROUND);
            size_t end = p->pos, k;

            if (job == NULL) {
                p->error = ERROR_MEMORY;
                return -1;
            }
            job->type = NODE_BACKGROUND;
            while (end > sourceStart && is_blank(p->text[end - 1]))
                end--;
            job->text = malloc(end - sourceStart + 1);
            if (job->text == NULL) {
                free_node(job);
                p->error = ERROR_MEMORY;
                return -1;
            }
            memcpy(job->text, p->text + sourceStart, end - sourceStart);
            job->text[end - sourceStart] = '\0';
            for (k = firstIndex; k < list->count; k++) {
                if (list_add(&job->first, list->nodes[k], k == firstIndex ? LIST_FIRST : list->ops[k]) != 0) {
                    // the nodes stay with the list, which frees them: only the shell of the job goes
                    free(job->first.nodes);
                    free(job->first.ops);
                    free(job->text);
                    free(job);
                    p->error = ERROR_MEMORY;
                    return -1;
                }
            }
            list->count = firstIndex;       // moved into the job
            p->pos++;                       // the &
            if (list_add(list, job, firstOp) != 0) {
                free_node(job);
                p->error = ERROR_MEMORY;
                return -1;
            }
        }
        return 0;                       // ; newline end: the caller skips it
    }
}

static int parse_if(Parser* p, Node* node) {
    static const char* const toThen[] = { "then", NULL };
    static const char* const afterThen[] = { "elif", "else", "fi", NULL };
    static const char* const toFi[] = { "fi", NULL };

    // the word `if` (or `elif`) is consumed
    if (parse_list(p, &node->first, toThen) != 0)
        return -1;
    if (node->first.count == 0)
        return syntax(p, "Syntax error: missing command after 'if'%s", NULL);
    if (expect_word(p, "then") != 0)
        return -1;
    if (parse_list(p, &node->second, afterThen) != 0)
        return -1;
    if (node->second.count == 0)
        return syntax(p, "Syntax error: missing command after 'then'%s", NULL);

    if (skip_separators(p, 1) != 0)
        return -1;
    if (at_word(p, "elif")) {
        Node* inner = new_node(NODE_IF);

        if (inner == NULL) {
            p->error = ERROR_MEMORY;
            return -1;
        }
        inner->type = NODE_IF;
        p->pos += 4;
        node->hasElse = 1;
        if (list_add(&node->third, inner, LIST_FIRST) != 0) {
            free_node(inner);
            p->error = ERROR_MEMORY;
            return -1;
        }
        return parse_if(p, inner);          // the inner one ends with the fi that ends this one
    }
    if (at_word(p, "else")) {
        p->pos += 4;
        node->hasElse = 1;
        if (parse_list(p, &node->third, toFi) != 0)
            return -1;
        if (node->third.count == 0)
            return syntax(p, "Syntax error: missing command after 'else'%s", NULL);
    }
    return expect_word(p, "fi");
}

static int parse_while(Parser* p, Node* node) {
    static const char* const toDo[] = { "do", NULL };
    static const char* const toDone[] = { "done", NULL };

    if (parse_list(p, &node->first, toDo) != 0)
        return -1;
    if (node->first.count == 0)
        return syntax(p, "Syntax error: missing command after '%s'", node->until ? "until" : "while");
    if (expect_word(p, "do") != 0)
        return -1;
    if (parse_list(p, &node->second, toDone) != 0)
        return -1;
    if (node->second.count == 0)
        return syntax(p, "Syntax error: missing command after 'do'%s", NULL);
    return expect_word(p, "done");
}

static int parse_for(Parser* p, Node* node) {
    static const char* const toDone[] = { "done", NULL };
    size_t start, end;
    char word[128];

    skip_blanks(p);
    if (!plain_word(p, p->pos, word, sizeof word, &end) || !(isalpha((unsigned char)word[0]) || word[0] == '_'))
        return syntax(p, "Syntax error: 'for' needs a variable name%s", NULL);
    for (start = 0; word[start] != '\0'; start++)
        if (!(isalnum((unsigned char)word[start]) || word[start] == '_'))
            return syntax(p, "Syntax error: '%s' is not a valid name", word);
    node->name = _strdup(word);
    if (node->name == NULL) {
        p->error = ERROR_MEMORY;
        return -1;
    }
    p->pos = end;

    skip_blanks(p);
    if (at_word(p, "in")) {
        size_t wordsEnd;

        p->pos += 2;
        skip_blanks(p);
        start = p->pos;
        wordsEnd = scan_simple(p, start);
        if (wordsEnd == (size_t)-1)
            return -1;
        if (p->text[wordsEnd] != '\0' && p->text[wordsEnd] != '\n' && p->text[wordsEnd] != ';')
            return syntax(p, "Syntax error: unexpected token in the words of 'for'%s", NULL);
        node->text = malloc(wordsEnd - start + 1);
        if (node->text == NULL) {
            p->error = ERROR_MEMORY;
            return -1;
        }
        memcpy(node->text, p->text + start, wordsEnd - start);
        node->text[wordsEnd - start] = '\0';
        p->pos = wordsEnd;
    }

    if (expect_word(p, "do") != 0)
        return -1;
    if (parse_list(p, &node->second, toDone) != 0)
        return -1;
    if (node->second.count == 0)
        return syntax(p, "Syntax error: missing command after 'do'%s", NULL);
    return expect_word(p, "done");
}

/**
 * What may follow a compound command: the end of the command, && or ||, or redirections of it (`done < file`,
 * `fi > out 2>&1`), which are kept as text for the redirection parser at the time it runs.
 */
static int parse_trailer(Parser* p, Node* node) {
    char c, next;

    skip_blanks(p);
    c = p->text[p->pos];
    next = c != '\0' ? p->text[p->pos + 1] : '\0';
    if (c == '\0' || c == '\n' || c == ';' || c == '#' || (c == '&' && next == '&') || (c == '|' && next == '|') ||
        (c == ')' && p->subshellDepth > 0))
        return 0;
    if (c == '|')
        return 0;                       // the next stage of a pipeline: parse_command goes on
    if (c == '&' && next != '&' && next != '>')
        return 0;                       // a background job: parse_andor takes the &
    if (c == '<' || c == '>' || (c == '&' && next == '>') || (isdigit((unsigned char)c) && next == '>')) {
        size_t start = p->pos, end;

        p->stopAtPipe = 1;
        end = scan_simple(p, start);
        p->stopAtPipe = 0;
        if (end == (size_t)-1)
            return -1;
        node->redirects = malloc(end - start + 1);
        if (node->redirects == NULL) {
            p->error = ERROR_MEMORY;
            return -1;
        }
        memcpy(node->redirects, p->text + start, end - start);
        node->redirects[end - start] = '\0';
        p->pos = end;
        return 0;
    }
    {
        char word[40];
        size_t n = 0;

        while (!is_word_end(p->text[p->pos + n]) && n < sizeof word - 1) {
            word[n] = p->text[p->pos + n];
            n++;
        }
        word[n] = '\0';
        return syntax(p, "Syntax error near unexpected token '%s'", word);
    }
}


/** Reads one word as it is written (quotes and substitutions in it passed) from pos; returns its end. */
static size_t scan_word(Parser* p, size_t pos) {
    const char* text = p->text;
    char quote = '\0';

    for (;;) {
        char c = text[pos];
        size_t passed;

        if (c == '\0')
            return pos;
        if (quote != '\'' && ((c == '$' && (text[pos + 1] == '(' || text[pos + 1] == '{')) || c == '`')) {
            size_t n = winshell_substitution_length(text + pos);

            if (n == 0)
                return pos;
            pos += n;
            continue;
        }
        passed = winshell_pass_quoted(text, pos, &quote);
        if (passed > 0) {
            pos += passed;
            continue;
        }
        if (is_blank(c) || c == '\n' || c == ';' || c == '&' || c == '|' || c == '<' || c == '>' || c == ')')
            return pos;
        pos++;
    }
}

static int add_pattern(CaseClause* clause, const char* start, size_t length) {
    char** grown;
    char* copy;

    while (length > 0 && is_blank(start[0])) {
        start++;
        length--;
    }
    while (length > 0 && is_blank(start[length - 1]))
        length--;
    copy = malloc(length + 1);
    grown = realloc(clause->patterns, (clause->patternCount + 1) * sizeof *grown);
    if (copy == NULL || grown == NULL) {
        free(copy);
        if (grown != NULL)
            clause->patterns = grown;
        return -1;
    }
    memcpy(copy, start, length);
    copy[length] = '\0';
    grown[clause->patternCount++] = copy;
    clause->patterns = grown;
    return 0;
}

static int parse_case(Parser* p, Node* node) {
    static const char* const toEsac[] = { "esac", NULL };
    size_t start, end;

    skip_blanks(p);
    start = p->pos;
    end = scan_word(p, start);
    if (end == start) {
        if (p->text[p->pos] == '\0')
            return incomplete(p, "Syntax error: unexpected end of input (expected a word after 'case')");
        return syntax(p, "Syntax error: 'case' needs a word%s", NULL);
    }
    node->text = malloc(end - start + 1);
    if (node->text == NULL) {
        p->error = ERROR_MEMORY;
        return -1;
    }
    memcpy(node->text, p->text + start, end - start);
    node->text[end - start] = '\0';
    p->pos = end;

    if (expect_word(p, "in") != 0)
        return -1;

    for (;;) {
        CaseClause* grown;
        CaseClause* clause;
        size_t patternStart;
        char quote = '\0';

        if (skip_separators(p, 0) != 0)
            return -1;
        if (p->text[p->pos] == '\0')
            return incomplete(p, "Syntax error: unexpected end of input (expected 'esac')");
        if (at_word(p, "esac")) {
            p->pos += 4;
            return 0;
        }
        if (p->text[p->pos] == '(')
            p->pos++;                   // the optional ( in front of the patterns

        grown = realloc(node->clauses, (node->clauseCount + 1) * sizeof *grown);
        if (grown == NULL) {
            p->error = ERROR_MEMORY;
            return -1;
        }
        node->clauses = grown;
        clause = &node->clauses[node->clauseCount];
        memset(clause, 0, sizeof *clause);
        node->clauseCount++;

        // the patterns: up to the ) outside of quotes, separated by |
        patternStart = p->pos;
        for (;;) {
            char c = p->text[p->pos];
            size_t passed;

            if (c == '\0')
                return incomplete(p, "Syntax error: unexpected end of input (expected ')' of a pattern)");
            if (quote != '\'' && ((c == '$' && (p->text[p->pos + 1] == '(' || p->text[p->pos + 1] == '{')) || c == '`')) {
                size_t n = winshell_substitution_length(p->text + p->pos);

                if (n > 0) {
                    p->pos += n;
                    continue;
                }
            }
            passed = winshell_pass_quoted(p->text, p->pos, &quote);
            if (passed > 0) {
                p->pos += passed;
                continue;
            }
            if (c == '|') {
                if (add_pattern(clause, p->text + patternStart, p->pos - patternStart) != 0) {
                    p->error = ERROR_MEMORY;
                    return -1;
                }
                p->pos++;
                patternStart = p->pos;
                continue;
            }
            if (c == ')') {
                if (add_pattern(clause, p->text + patternStart, p->pos - patternStart) != 0) {
                    p->error = ERROR_MEMORY;
                    return -1;
                }
                p->pos++;
                break;
            }
            p->pos++;
        }
        {
            size_t k;
            for (k = 0; k < clause->patternCount; k++)
                if (clause->patterns[k][0] == '\0')
                    return syntax(p, "Syntax error: empty pattern in 'case'%s", NULL);
        }

        if (parse_list(p, &clause->body, toEsac) != 0)
            return -1;
        if (p->text[p->pos] == ';' && p->text[p->pos + 1] == ';')
            p->pos += 2;
        else if (!at_word(p, "esac")) {
            if (p->text[p->pos] == '\0')
                return incomplete(p, "Syntax error: unexpected end of input (expected 'esac')");
            return syntax(p, "Syntax error: expected ';;' or 'esac'%s", NULL);
        }
    }
}

static int parse_group(Parser* p, Node* node) {
    static const char* const toBrace[] = { "}", NULL };

    if (parse_list(p, &node->second, toBrace) != 0)
        return -1;
    if (node->second.count == 0)
        return syntax(p, "Syntax error: missing command after '{'%s", NULL);
    return expect_word(p, "}");
}

static int parse_subshell(Parser* p, Node* node) {
    p->subshellDepth++;
    if (parse_list(p, &node->second, NULL) != 0) {
        p->subshellDepth--;
        return -1;
    }
    p->subshellDepth--;
    if (node->second.count == 0)
        return syntax(p, "Syntax error: missing command after '('%s", NULL);
    if (skip_separators(p, 1) != 0)
        return -1;
    if (p->text[p->pos] == '\0')
        return incomplete(p, "Syntax error: unexpected end of input (expected ')')");
    if (p->text[p->pos] != ')')
        return syntax(p, "Syntax error: expected ')'%s", NULL);
    p->pos++;
    return 0;
}

static Node* parse_single(Parser* p) {
    char buffer[16];
    const char* word;
    Node* node;

    skip_blanks(p);
    if (p->text[p->pos] == '\0' || p->text[p->pos] == '\n') {
        incomplete(p, "Syntax error: missing command");
        return NULL;
    }

    // name() compound-command
    if (isalpha((unsigned char)p->text[p->pos]) || p->text[p->pos] == '_') {
        size_t nameEnd = p->pos, k, definitionStart = p->pos;

        while (isalnum((unsigned char)p->text[nameEnd]) || p->text[nameEnd] == '_')
            nameEnd++;
        k = nameEnd;
        while (is_blank(p->text[k]))
            k++;
        if (p->text[k] == '(') {
            k++;
            while (is_blank(p->text[k]))
                k++;
            if (p->text[k] == ')') {
                Node* function;
                Node* body;

                function = new_node(NODE_FUNCTION);
                if (function == NULL) {
                    p->error = ERROR_MEMORY;
                    return NULL;
                }
                function->type = NODE_FUNCTION;
                function->name = malloc(nameEnd - p->pos + 1);
                if (function->name == NULL) {
                    free_node(function);
                    p->error = ERROR_MEMORY;
                    return NULL;
                }
                memcpy(function->name, p->text + p->pos, nameEnd - p->pos);
                function->name[nameEnd - p->pos] = '\0';
                p->pos = k + 1;
                while (is_blank(p->text[p->pos]) || p->text[p->pos] == '\n')
                    p->pos++;
                if (p->text[p->pos] == '\0') {
                    free_node(function);
                    incomplete(p, "Syntax error: unexpected end of input (the body of a function)");
                    return NULL;
                }
                body = parse_command(p);
                if (body == NULL) {
                    free_node(function);
                    return NULL;
                }
                if (body->type == NODE_SIMPLE || body->type == NODE_PIPE) {
                    free_node(body);
                    free_node(function);
                    syntax(p, "Syntax error: the body of a function has to be a compound command%s", NULL);
                    return NULL;
                }
                if (list_add(&function->second, body, LIST_FIRST) != 0) {
                    free_node(body);
                    free_node(function);
                    p->error = ERROR_MEMORY;
                    return NULL;
                }
                // the text of the definition, for a background job that needs the function
                function->text = malloc(p->pos - definitionStart + 1);
                if (function->text != NULL) {
                    memcpy(function->text, p->text + definitionStart, p->pos - definitionStart);
                    function->text[p->pos - definitionStart] = '\0';
                }
                return function;
            }
        }
    }

    // ( list ): a subshell
    if (p->text[p->pos] == '(') {
        int status;

        node = new_node(NODE_SUBSHELL);
        if (node == NULL) {
            p->error = ERROR_MEMORY;
            return NULL;
        }
        node->type = NODE_SUBSHELL;
        p->pos++;
        status = parse_subshell(p, node);
        if (status != 0 || parse_trailer(p, node) != 0) {
            free_node(node);
            return NULL;
        }
        return node;
    }

    word = reserved_here(p, buffer, sizeof buffer);
    if (word != NULL) {
        NodeType type;
        int status;

        if (strcmp(word, "if") == 0)
            type = NODE_IF;
        else if (strcmp(word, "while") == 0 || strcmp(word, "until") == 0)
            type = NODE_WHILE;
        else if (strcmp(word, "for") == 0)
            type = NODE_FOR;
        else if (strcmp(word, "case") == 0)
            type = NODE_CASE;
        else if (strcmp(word, "{") == 0)
            type = NODE_GROUP;
        else {
            syntax(p, "Syntax error near unexpected token '%s'", word);
            return NULL;
        }
        node = new_node(type);
        if (node == NULL) {
            p->error = ERROR_MEMORY;
            return NULL;
        }
        node->type = type;
        node->until = strcmp(word, "until") == 0;
        p->pos += strlen(word);
        if (type == NODE_IF)
            status = parse_if(p, node);
        else if (type == NODE_WHILE)
            status = parse_while(p, node);
        else if (type == NODE_FOR)
            status = parse_for(p, node);
        else if (type == NODE_CASE)
            status = parse_case(p, node);
        else
            status = parse_group(p, node);
        if (status != 0) {
            free_node(node);
            return NULL;
        }
        if (parse_trailer(p, node) != 0) {
            free_node(node);
            return NULL;
        }
        return node;
    }

    // a simple command: up to the next ; newline && ||
    {
        size_t start = p->pos, end = scan_simple(p, p->pos);

        if (end == (size_t)-1)
            return NULL;
        if (end == start) {
            // an operator with no command in front of it
            char shown[3] = { p->text[start], p->text[start] == '&' || p->text[start] == '|' ? p->text[start] : '\0', '\0' };

            syntax(p, "Syntax error near unexpected token '%s'", shown);
            return NULL;
        }
        node = new_node(NODE_SIMPLE);
        if (node == NULL) {
            p->error = ERROR_MEMORY;
            return NULL;
        }
        node->type = NODE_SIMPLE;
        node->text = malloc(end - start + 1);
        if (node->text == NULL) {
            free(node);
            p->error = ERROR_MEMORY;
            return NULL;
        }
        memcpy(node->text, p->text + start, end - start);
        node->text[end - start] = '\0';
        p->pos = end;

        return node;
    }
}

/**
 * A command and the stages after it when they are joined with | and one of them is a compound command: the
 * stages are the nodes of a NODE_PIPE. (Stages that are all simple commands stay in one text: a pipeline.)
 */
static Node* parse_command(Parser* p) {
    Node* first = parse_single(p);
    Node* pipe;

    if (first == NULL)
        return NULL;
    skip_blanks(p);
    if (!p->pipeToCompound && !(p->text[p->pos] == '|' && p->text[p->pos + 1] != '|'))
        return first;

    pipe = new_node(NODE_PIPE);
    if (pipe == NULL) {
        free_node(first);
        p->error = ERROR_MEMORY;
        return NULL;
    }
    pipe->type = NODE_PIPE;
    if (list_add(&pipe->first, first, LIST_FIRST) != 0) {
        free_node(first);
        free_node(pipe);
        p->error = ERROR_MEMORY;
        return NULL;
    }
    for (;;) {
        Node* next;

        skip_blanks(p);
        if (!p->pipeToCompound && !(p->text[p->pos] == '|' && p->text[p->pos + 1] != '|'))
            break;
        p->pipeToCompound = 0;
        p->pos++;                       // the |
        while (is_blank(p->text[p->pos]) || p->text[p->pos] == '\n')
            p->pos++;
        next = parse_single(p);
        if (next == NULL) {
            free_node(pipe);
            return NULL;
        }
        if (list_add(&pipe->first, next, LIST_FIRST) != 0) {
            free_node(next);
            free_node(pipe);
            p->error = ERROR_MEMORY;
            return NULL;
        }
    }
    return pipe;
}

/* ---- running ----------------------------------------------------------------------------- */

typedef struct {
    int* exitShell;
    int  loopDepth;
} Runner;

static int run_list(Runner* r, const List* list, int status);

// break and continue: how many loops still have to be left, and whether the last of them goes on
static int pendingLevels;
static int pendingContinue;
static volatile LONG interruptRequested;
static int activeLoopDepth;

void command_interrupt(void) {
    InterlockedExchange(&interruptRequested, 1);
}

int command_was_interrupted(void) {
    return InterlockedCompareExchange(&interruptRequested, 0, 0) != 0;
}

int command_break(int levels, int isContinue) {
    if (activeLoopDepth == 0) {
        fprintf(stderr, "%s: only meaningful in a `for', `while', or `until' loop\n", isContinue ? "continue" : "break");
        return 1;
    }
    if (levels < 1)
        levels = 1;
    if (levels > activeLoopDepth)
        levels = activeLoopDepth;
    pendingLevels = levels;
    pendingContinue = isContinue;
    return 0;
}

static int interrupted_now(void) {
    return InterlockedCompareExchange(&interruptRequested, 0, 0) != 0;
}

/* ---- traps ------------------------------------------------------------------------------- */

enum { TRAP_EXIT, TRAP_INT, TRAP_ERR, TRAP_COUNT };

// NULL: the default action; "": ignored; otherwise the commands to run
static char* trapAction[TRAP_COUNT];
static int trapRunning[TRAP_COUNT];
static int errSuppressed;               // inside a condition, a function or a trap: no ERR

static const char* const trapNames[TRAP_COUNT] = { "EXIT", "INT", "ERR" };

/** Runs the commands of a trap. Returns the status to go on with; *exitShell is set when they ended the shell. */
static int run_trap(int which, int* exitShell, int status) {
    char* text;
    int savedLevels = pendingLevels, savedContinue = pendingContinue, result;
    int savedSuppressed = errSuppressed;

    if (trapAction[which] == NULL || trapAction[which][0] == '\0' || trapRunning[which])
        return status;
    text = _strdup(trapAction[which]);
    if (text == NULL)
        return status;
    trapRunning[which] = 1;
    errSuppressed = 1;
    pendingLevels = 0;
    winshell_setLastStatus(status);
    result = command_run_text(text, exitShell, status);
    free(text);
    errSuppressed = savedSuppressed;
    trapRunning[which] = 0;
    pendingLevels = savedLevels;
    pendingContinue = savedContinue;
    if (!*exitShell) {
        winshell_setLastStatus(status);     // $? is as it was before the trap
        return status;
    }
    return result;
}

/** With a trap on INT, a Ctrl+C is not the end of the running commands: the trap runs and they go on. */
static void service_interrupt(const Runner* r, int* status) {
    if (trapAction[TRAP_INT] == NULL || !interrupted_now())
        return;
    InterlockedExchange(&interruptRequested, 0);
    *status = run_trap(TRAP_INT, r->exitShell, *status);
}

/** Whether a Ctrl+C ends the loop that asks: it does not when there is a trap on INT. */
static int interrupt_ends_loop(const Runner* r, int status) {
    service_interrupt(r, &status);
    return interrupted_now();
}

void command_run_exit_trap(int* status) {
    int exitShell = 0;

    // an `exit N` inside the trap gives the status of the shell
    *status = run_trap(TRAP_EXIT, &exitShell, *status);
}

void* command_traps_enter_subshell(void) {
    char** saved = calloc(TRAP_COUNT, sizeof *saved);
    int i;

    if (saved == NULL)
        return NULL;
    for (i = 0; i < TRAP_COUNT; i++) {
        saved[i] = trapAction[i];
        // an ignored signal stays ignored; the other traps are reset in a subshell
        trapAction[i] = (saved[i] != NULL && saved[i][0] == '\0') ? _strdup("") : NULL;
    }
    return saved;
}

void command_traps_leave_subshell(void* handle) {
    char** saved = handle;
    int i;

    if (saved == NULL)
        return;
    for (i = 0; i < TRAP_COUNT; i++) {
        free(trapAction[i]);
        trapAction[i] = saved[i];
    }
    free(saved);
}

/** The trap that a signal name or number stands for; prints the reason and returns 1 when there is none. */
static int trap_slot(const char* name, int* slot) {
    static const char* const unsupported[] = { "HUP", "QUIT", "ILL", "ABRT", "FPE", "SEGV", "PIPE", "ALRM", "TERM",
                                               "USR1", "USR2", "CHLD", "CONT", "TSTP", "TTIN", "TTOU", "BUS",
                                               "DEBUG", "RETURN", "KILL", "STOP" };
    const char* n = name;
    char* end;
    long number;
    size_t i;

    if (strncmp(n, "SIG", 3) == 0)
        n += 3;
    if (strcmp(n, "EXIT") == 0) { *slot = TRAP_EXIT; return 0; }
    if (strcmp(n, "INT") == 0)  { *slot = TRAP_INT;  return 0; }
    if (strcmp(n, "ERR") == 0)  { *slot = TRAP_ERR;  return 0; }
    number = strtol(name, &end, 10);
    if (*name != '\0' && *end == '\0') {
        if (number == 0) { *slot = TRAP_EXIT; return 0; }
        if (number == 2) { *slot = TRAP_INT;  return 0; }
        if (number > 0 && number < 65) {
            fprintf(stderr, "trap: %s: signal not supported\n", name);
            return 1;
        }
    }
    for (i = 0; i < sizeof unsupported / sizeof unsupported[0]; i++) {
        if (strcmp(n, unsupported[i]) == 0) {
            fprintf(stderr, "trap: %s: signal not supported\n", name);
            return 1;
        }
    }
    fprintf(stderr, "trap: %s: invalid signal specification\n", name);
    return 1;
}

static void trap_print(int slot) {
    const char* p;

    if (trapAction[slot] == NULL)
        return;
    fputs("trap -- '", stdout);
    for (p = trapAction[slot]; *p != '\0'; p++) {
        if (*p == '\'')
            fputs("'\\''", stdout);
        else
            putchar(*p);
    }
    printf("' %s\n", trapNames[slot]);
}

static int trap_command(char* const* argv);

int command_trap(char* const* argv) {
    int status = trap_command(argv);

    fflush(stdout);                     // before what the commands after it write
    return status;
}

static int trap_command(char* const* argv) {
    int i = 1, status = 0, slot;
    const char* action;

    if (argv[i] != NULL && strcmp(argv[i], "--") == 0)
        i++;
    if (argv[i] != NULL && strcmp(argv[i], "-l") == 0) {
        puts("EXIT INT ERR");
        return 0;
    }
    if (argv[i] != NULL && strcmp(argv[i], "-p") == 0) {
        i++;
        if (argv[i] != NULL) {
            for (; argv[i] != NULL; i++)
                if (trap_slot(argv[i], &slot) == 0)
                    trap_print(slot);
                else
                    status = 1;
            return status;
        }
    }
    if (argv[i] == NULL) {
        for (slot = 0; slot < TRAP_COUNT; slot++)
            trap_print(slot);
        return 0;
    }
    if (argv[i + 1] == NULL) {
        // one operand: it is a signal, and its trap is reset
        if (trap_slot(argv[i], &slot) != 0)
            return 1;
        free(trapAction[slot]);
        trapAction[slot] = NULL;
        return 0;
    }

    action = argv[i++];
    for (; argv[i] != NULL; i++) {
        if (trap_slot(argv[i], &slot) != 0) {
            status = 1;
            continue;
        }
        free(trapAction[slot]);
        trapAction[slot] = strcmp(action, "-") == 0 ? NULL : _strdup(action);
    }
    return status;
}

/**
 * After the body of a loop: whether to leave the loop (1) or go on with the next round (0), and what a
 * break or continue inside did to the levels that remain.
 */
static int after_round(void) {
    if (pendingLevels > 0) {
        int isContinue = pendingContinue;

        pendingLevels--;
        if (pendingLevels > 0)
            return 1;                   // an outer loop is the target: leave this one, the rest goes on
        pendingContinue = 0;
        return isContinue ? 0 : 1;
    }
    return 0;
}

static int run_node(Runner* r, const Node* node, int status);


/* ---- functions ------------------------------------------------------------------------------- */

typedef struct {
    char* name;
    Node* body;                 // owned: taken from the tree of the definition
    char* source;               // the text of the definition (NULL when unknown)
} Function;

static Function* functions;
static size_t functionCount;
static int callDepth;
static int pendingReturn;
static int returnStatus;

typedef struct {
    char* name;
    char* old;                  // NULL: it was not set
} LocalVariable;

static LocalVariable* locals;
static size_t localCount;

static Function* find_function(const char* name) {
    size_t i;

    for (i = 0; i < functionCount; i++)
        if (strcmp(functions[i].name, name) == 0)
            return &functions[i];
    return NULL;
}

int command_has_function(const char* name) {
    return find_function(name) != NULL;
}

/** Defines the function, taking the body out of the tree. */
static int define_function(Node* node) {
    Function* function = find_function(node->name);
    Node* body = node->second.nodes[0];

    if (function != NULL) {
        free_node(function->body);
        free(function->source);
    } else {
        Function* grown = realloc(functions, (functionCount + 1) * sizeof *grown);

        if (grown == NULL)
            return -1;
        functions = grown;
        function = &functions[functionCount++];
        function->name = _strdup(node->name);
        if (function->name == NULL) {
            functionCount--;
            return -1;
        }
    }
    function->body = body;
    function->source = node->text != NULL ? _strdup(node->text) : NULL;
    node->second.nodes[0] = NULL;       // the table owns it now
    return 0;
}

int command_unset_function(const char* name) {
    size_t i;

    for (i = 0; i < functionCount; i++) {
        if (strcmp(functions[i].name, name) == 0) {
            free(functions[i].name);
            free(functions[i].source);
            free_node(functions[i].body);
            functions[i] = functions[--functionCount];
            return 1;
        }
    }
    return 0;
}

int command_return(int status) {
    if (callDepth == 0) {
        fprintf(stderr, "return: can only `return' from a function\n");
        return 1;
    }
    pendingReturn = 1;
    returnStatus = status & 0xFF;
    pendingLevels = 1 << 20;            // stops the lists and the loops of the function, up to its call
    pendingContinue = 0;
    return 0;
}

int command_local(const char* name, const char* value) {
    LocalVariable* grown;
    const char* old;

    if (callDepth == 0) {
        fprintf(stderr, "local: can only be used in a function\n");
        return 1;
    }
    grown = realloc(locals, (localCount + 1) * sizeof *grown);
    if (grown == NULL)
        return 1;
    locals = grown;
    old = var_get(name);
    locals[localCount].name = _strdup(name);
    locals[localCount].old = old != NULL ? _strdup(old) : NULL;
    localCount++;
    if (value != NULL)
        var_set(name, value);
    else
        var_unset(name);
    return 0;
}

static int run_function(Runner* r, const Function* function, char* const* argv, int status) {
    ParameterFrame* frame;
    size_t localsBefore = localCount, i;
    int count = 0, result;

    if (callDepth >= 200) {
        fprintf(stderr, "%s: maximum function nesting level exceeded\n", argv[0]);
        return 1;
    }
    while (argv[1 + count] != NULL)
        count++;
    frame = interp_parameters_push((const char* const*)argv + 1, count);
    callDepth++;
    activeLoopDepth += 0;
    errSuppressed++;
    result = run_node(r, function->body, status);
    errSuppressed--;
    if (pendingReturn) {
        result = returnStatus;
        pendingReturn = 0;
        pendingLevels = 0;
    }
    callDepth--;
    for (i = localCount; i > localsBefore; i--) {
        LocalVariable* local = &locals[i - 1];

        if (local->old != NULL)
            var_set(local->name, local->old);
        else
            var_unset(local->name);
        free(local->name);
        free(local->old);
    }
    localCount = localsBefore;
    interp_parameters_pop(frame);
    return result;
}

int command_call_function(const char* name, char* const* argv, int* exitShell, int status) {
    Runner runner;
    Function* function = find_function(name);

    if (function == NULL)
        return 127;
    runner.exitShell = exitShell;
    runner.loopDepth = 0;
    return run_function(&runner, function, argv, status);
}

/** The text of a case word or pattern, expanded as the word of a command is, but not split or matched to files. */
static char* expand_case_word(const char* text, char** pattern) {
    Pipeline pipeline;
    char* line = malloc(strlen(text) + 8);
    char* word = NULL;

    *pattern = NULL;
    if (line == NULL)
        return NULL;
    sprintf(line, "x \"\" %s", text);
    if (winshell_parse(line, &pipeline) != 0) {
        free(line);
        return NULL;
    }
    free(line);
    if (pipeline.count > 0 && pipeline.commands[0].argv != NULL && pipeline.commands[0].argv[2] != NULL) {
        const Command* c = &pipeline.commands[0];
        size_t total = 1, i;

        // all the words that the text became are one: they are joined with a space
        for (i = 2; c->argv[i] != NULL; i++)
            total += strlen(c->argv[i]) + 1;
        word = malloc(total);
        *pattern = malloc(total * 2 + 1);
        if (word != NULL && *pattern != NULL) {
            word[0] = '\0';
            (*pattern)[0] = '\0';
            for (i = 2; c->argv[i] != NULL; i++) {
                if (i > 2) {
                    strcat(word, " ");
                    strcat(*pattern, " ");
                }
                strcat(word, c->argv[i]);
                if (c->patterns != NULL && c->patterns[i] != NULL) {
                    strcat(*pattern, c->patterns[i]);
                } else {
                    // a word with no active wildcard (all quoted): every wildcard in it is plain
                    const char* from;
                    size_t used = strlen(*pattern);

                    for (from = c->argv[i]; *from != '\0'; from++) {
                        if (*from == '*' || *from == '?' || *from == '[' || *from == '\\')
                            (*pattern)[used++] = GLOB_ESCAPE;
                        (*pattern)[used++] = *from;
                    }
                    (*pattern)[used] = '\0';
                }
            }
        }
    } else {
        word = _strdup("");
        *pattern = _strdup("");
    }
    winshell_freePipeline(&pipeline);
    return word;
}

static int run_case(Runner* r, const Node* node, int status) {
    char* pattern = NULL;
    char* word = expand_case_word(node->text, &pattern);
    size_t i, k;
    int result = 0;

    free(pattern);
    if (word == NULL)
        return 1;
    for (i = 0; i < node->clauseCount; i++) {
        const CaseClause* clause = &node->clauses[i];
        int matched = 0;

        for (k = 0; k < clause->patternCount && !matched; k++) {
            char* literal = NULL;
            char* text = expand_case_word(clause->patterns[k], &literal);

            if (text != NULL && literal != NULL)
                matched = winshell_pattern_matches(literal, word);
            free(text);
            free(literal);
        }
        if (matched) {
            result = run_list(r, &clause->body, status);
            break;
        }
    }
    free(word);
    return result;
}

/** ( list ): runs in a subshell, as far as that can be done in the process (see $(...)): exit, cd and the variables stay inside. */
static int run_subshell(Runner* r, const Node* node, int status) {
    char directory[VPATH_MAX];
    VarSnapshot* variables;
    void* traps;
    Positionals* positionals;
    int exitInside = 0, result;
    Runner inner;

    cwd_get(directory, sizeof directory);
    variables = var_save();
    traps = command_traps_enter_subshell();
    positionals = winshell_positionals_save();
    inner.exitShell = &exitInside;
    inner.loopDepth = 0;
    result = run_list(&inner, &node->second, status);
    command_traps_leave_subshell(traps);
    winshell_positionals_restore(positionals);
    if (variables != NULL) {
        char absolute[VPATH_MAX];

        cwd_change(directory, absolute, sizeof absolute);
        var_restore(variables);
    }
    (void)r;
    return result;
}
/** A compound command with the redirections written after it: they are in force while it runs. */
static int run_redirected(Runner* r, const Node* node, int status) {
    Pipeline pipeline;
    StdRedirection saved;
    Node copy;
    char* line = malloc(strlen(node->redirects) + 4);
    int result;

    if (line == NULL)
        return 1;
    sprintf(line, "x %s", node->redirects);
    if (winshell_parse(line, &pipeline) != 0) {
        free(line);
        return 2;
    }
    free(line);
    if (pipeline.count != 1) {
        winshell_freePipeline(&pipeline);
        return 2;
    }
    if (redirect_begin(&pipeline.commands[0], &saved) != 0) {
        winshell_freePipeline(&pipeline);
        return 1;
    }
    if (saved.inputRedirected)
        interp_input_redirected(1);
    copy = *node;
    copy.redirects = NULL;              // not again
    result = run_node(r, &copy, status);
    if (saved.inputRedirected)
        interp_input_redirected(-1);
    redirect_end(&saved);
    winshell_freePipeline(&pipeline);
    return result;
}

/* ---- pipelines with compound commands or functions --------------------------------------------- */

/*
 * The stages of such a pipeline run one after the other: every stage but the last runs in a subshell with
 * its output captured, and the captured bytes are the input of the next stage. The last stage runs in the
 * current shell. A stage that never ends (`yes | f`) therefore cannot be used, and `head` at the end does
 * not stop the one in front of it early.
 */

typedef struct {
    const Node* node;           // a compound command, or
    char*       text;           // a pipeline of simple commands (owned when `owned`)
    int         owned;
} Stage;

typedef struct {
    Stage* items;
    size_t count, capacity;
} StageList;

static int stages_add(StageList* list, const Node* node, char* text, int owned) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        Stage* grown = realloc(list->items, capacity * sizeof *grown);

        if (grown == NULL)
            return -1;
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count].node = node;
    list->items[list->count].text = text;
    list->items[list->count].owned = owned;
    list->count++;
    return 0;
}

static void stages_free(StageList* list) {
    size_t i;

    for (i = 0; i < list->count; i++)
        if (list->items[i].owned)
            free(list->items[i].text);
    free(list->items);
    memset(list, 0, sizeof *list);
}

/** Cuts the text of a pipeline at its | (not inside quotes and substitutions). The parts are malloc'ed. */
static int split_pipeline_text(const char* text, char*** parts, size_t* count) {
    size_t i = 0, start = 0, n = 0, capacity = 0;
    char quote = '\0';
    char** list = NULL;

    for (;; ) {
        char c = text[i];
        int end = c == '\0';

        if (!end) {
            size_t passed;

            if (quote != '\'' && ((c == '$' && (text[i + 1] == '(' || text[i + 1] == '{')) || c == '`')) {
                size_t length = winshell_substitution_length(text + i);

                i += length > 0 ? length : 1;
                continue;
            }
            passed = winshell_pass_quoted(text, i, &quote);
            if (passed > 0) {
                i += passed;
                continue;
            }
        }
        if (end || (c == '|' && text[i + 1] != '|' && (i == 0 || text[i - 1] != '>'))) {
            char* part = malloc(i - start + 1);

            if (part == NULL || VEC_RESERVE(list, capacity, n + 2) != 0) {
                free(part);
                while (n > 0)
                    free(list[--n]);
                free(list);
                return -1;
            }
            memcpy(part, text + start, i - start);
            part[i - start] = '\0';
            list[n++] = part;
            if (end)
                break;
            start = i + 1;
        }
        i++;
    }
    *parts = list;
    *count = n;
    return 0;
}

/** A function or a builtin: it has to run in the shell itself, not as a program. */
static int part_is_function_call(const char* part) {
    char name[128];
    size_t n = 0;

    while (is_blank(*part))
        part++;
    while (*part != '\0' && !is_blank(*part) && strchr("<>;&()\"'$`\\", *part) == NULL && n < sizeof name - 1)
        name[n++] = *part++;
    name[n] = '\0';
    return n > 0 && (command_has_function(name) || builtin_exists(name));
}

/** Adds the stages of a pipeline text: the parts that call a function stand alone, the others stay together. */
static int append_text_stages(StageList* list, const char* text, int* anyFunction) {
    char** parts;
    size_t count, i, groupStart = 0;
    int result = 0;

    if (strchr(text, '|') == NULL || split_pipeline_text(text, &parts, &count) != 0 || count < 2) {
        char* copy = _strdup(text);

        if (copy == NULL || stages_add(list, NULL, copy, 1) != 0) {
            free(copy);
            return -1;
        }
        if (strchr(text, '|') == NULL) {
            char* word = _strdup(text);
            *anyFunction |= word != NULL && part_is_function_call(word);
            free(word);
        }
        return 0;
    }

    for (i = 0; i <= count; i++) {
        int function = i < count && part_is_function_call(parts[i]);

        if (i == count || function) {
            if (i > groupStart) {
                size_t total = 1, k;
                char* joined;

                for (k = groupStart; k < i; k++)
                    total += strlen(parts[k]) + 1;
                joined = malloc(total);
                if (joined == NULL) {
                    result = -1;
                    break;
                }
                joined[0] = '\0';
                for (k = groupStart; k < i; k++) {
                    if (k > groupStart)
                        strcat(joined, "|");
                    strcat(joined, parts[k]);
                }
                if (stages_add(list, NULL, joined, 1) != 0) {
                    free(joined);
                    result = -1;
                    break;
                }
            }
            if (function) {
                *anyFunction = 1;
                if (stages_add(list, NULL, parts[i], 1) != 0) {
                    result = -1;
                    break;
                }
                parts[i] = NULL;        // the list owns it now
            }
            groupStart = i + 1;
        }
    }
    for (i = 0; i < count; i++)
        free(parts[i]);
    free(parts);
    return result;
}

typedef struct {
    const Stage* stage;
    int          status;
} StageRun;

static int run_stage_here(Runner* r, const Stage* stage, int status) {
    if (stage->node != NULL)
        return run_node(r, stage->node, status);
    return interp_run_line(stage->text, r->exitShell, status);
}

static int run_stage_captured(void* context) {
    const StageRun* run = context;
    Runner inner;
    int exitInside = 0;

    inner.exitShell = &exitInside;
    inner.loopDepth = 0;
    return run_stage_here(&inner, run->stage, run->status);
}

static int run_stages(Runner* r, const StageList* list, int status) {
    InputRedirection* input = NULL;
    int result = 0;
    size_t i;

    for (i = 0; i < list->count; i++) {
        if (i + 1 == list->count) {
            result = run_stage_here(r, &list->items[i], status);
        } else {
            StageRun run;
            char* output = NULL;
            size_t length = 0;
            int stageStatus;

            run.stage = &list->items[i];
            run.status = status;
            if (interp_capture_call(run_stage_captured, &run, &output, &length, &stageStatus) != 0) {
                result = 1;
                break;
            }
            if (input != NULL) {
                interp_input_redirected(-1);
                interp_input_restore(input);
            }
            input = interp_input_from_bytes(output, length);
            free(output);
            if (input == NULL) {
                fprintf(stderr, "Error: Cannot make the input of the command\n");
                result = 1;
                break;
            }
            interp_input_redirected(1);
        }
    }
    if (input != NULL) {
        interp_input_redirected(-1);
        interp_input_restore(input);
    }
    return result;
}

/** A pipeline in which a compound command is a stage. */
static int run_pipe(Runner* r, const Node* node, int status) {
    StageList list;
    size_t i;
    int ignored = 0, result;

    memset(&list, 0, sizeof list);
    for (i = 0; i < node->first.count; i++) {
        const Node* stage = node->first.nodes[i];

        if ((stage->type == NODE_SIMPLE ? append_text_stages(&list, stage->text, &ignored)
                                        : stages_add(&list, stage, NULL, 0)) != 0) {
            stages_free(&list);
            return 1;
        }
    }
    result = run_stages(r, &list, status);
    stages_free(&list);
    return result;
}

/** A simple command whose pipeline calls a function runs as stages; any other goes to the pipeline runner. */
static int run_simple(Runner* r, const Node* node, int status) {
    StageList list;
    int anyFunction = 0, result;

    if (strchr(node->text, '|') == NULL)
        return interp_run_simple(node->text, r->exitShell, status);
    memset(&list, 0, sizeof list);
    if (append_text_stages(&list, node->text, &anyFunction) != 0 || !anyFunction) {
        stages_free(&list);
        return interp_run_simple(node->text, r->exitShell, status);
    }
    result = run_stages(r, &list, status);
    stages_free(&list);
    return result;
}

/* ---- background jobs ----------------------------------------------------------------------- */

/*
 * `list &`. A pipeline of programs is started at once without waiting for it (its standard input is NUL,
 * as for a job of a shell without job control). Anything else (builtins, functions, compound commands,
 * and-or lists) runs in a shell of its own, `shell -c`, which is given what it needs from this one: the
 * variables that are not exported, the functions and the parameters. The job is in the table of jobs.c.
 */

typedef struct {
    char*  data;
    size_t length, capacity;
} TextBuffer;

static int text_append(TextBuffer* b, const char* text, size_t length) {
    if (VEC_RESERVE(b->data, b->capacity, b->length + length + 1) != 0)
        return -1;
    memcpy(b->data + b->length, text, length);
    b->length += length;
    b->data[b->length] = '\0';
    return 0;
}

static void add_local_variable(const char* name, const char* value, void* context) {
    TextBuffer* b = context;
    const char* c;

    if (!(isalpha((unsigned char)name[0]) || name[0] == '_'))
        return;                         // the parameters go as arguments
    for (c = name; *c != '\0'; c++)
        if (!isalnum((unsigned char)*c) && *c != '_')
            return;
    text_append(b, name, strlen(name));
    text_append(b, "='", 2);
    for (c = value; *c != '\0'; c++) {
        if (*c == '\'')
            text_append(b, "'\\''", 4);
        else
            text_append(b, c, 1);
    }
    text_append(b, "'\n", 2);
}

/** The functions, the variables of the shell and then the commands of the job, as the text of a script. */
static char* background_script(const char* text) {
    TextBuffer b = { NULL, 0, 0 };
    size_t i;

    for (i = 0; i < functionCount; i++) {
        if (functions[i].source != NULL) {
            text_append(&b, functions[i].source, strlen(functions[i].source));
            text_append(&b, "\n", 1);
        }
    }
    var_each_local(add_local_variable, &b);
    if (text_append(&b, text, strlen(text)) != 0) {
        free(b.data);
        return NULL;
    }
    return b.data;
}

/** Whether every stage of the pipeline text is a program (not a builtin, a function or a bare assignment). */
static int background_is_programs(const char* text) {
    char** parts;
    size_t count, i;
    int programs = 1;

    if (split_pipeline_text(text, &parts, &count) != 0)
        return 0;
    for (i = 0; i < count; i++) {
        const char* word = parts[i];
        size_t n;

        while (is_blank(*word))
            word++;
        n = strcspn(word, " \t=");
        if (word[n] == '=' || n == 0 || part_is_function_call(parts[i]))
            programs = 0;
        free(parts[i]);
    }
    free(parts);
    return programs;
}

static int run_background(Runner* r, const Node* node, int status) {
    const Node* only = node->first.count == 1 ? node->first.nodes[0] : NULL;
    char label[512];
    char* script;
    char* arguments[16];
    int count = 0, i, started;
    const char* number;
    const char* name;

    (void)r;
    (void)status;
    snprintf(label, sizeof label, "%s &", node->text);

    if (only != NULL && only->type == NODE_SIMPLE && background_is_programs(only->text)) {
        Pipeline pipeline;
        HANDLE* processes = NULL;
        size_t processCount = 0;

        if (winshell_parse(only->text, &pipeline) != 0)
            return 2;
        if (pipeline.count > 0 && winshell_glob(&pipeline) == 0 &&
            execute_pipeline_background(&pipeline, &processes, &processCount) == 0)
            jobs_add(processes, processCount, label);
        winshell_freePipeline(&pipeline);
        return 0;
    }

    script = background_script(node->text);
    if (script == NULL)
        return 1;
    number = var_get("#");
    name = var_get("0");
    count = number != NULL ? atoi(number) : 0;
    if (count > 9)
        count = 9;
    {
        char executable[MAX_PATH];
        char names[9][3];

        if (GetModuleFileNameA(NULL, executable, sizeof executable) == 0) {
            free(script);
            return 1;
        }
        arguments[0] = executable;
        arguments[1] = "-c";
        arguments[2] = script;
        arguments[3] = (char*)(name != NULL ? name : "shell");
        for (i = 0; i < count; i++) {
            const char* value;

            snprintf(names[i], sizeof names[i], "%d", i + 1);
            value = var_get(names[i]);
            arguments[4 + i] = (char*)(value != NULL ? value : "");
        }
        arguments[4 + count] = NULL;
        started = jobs_start_shell(arguments, label);
    }
    free(script);
    return started < 0 ? 1 : 0;
}

static int run_node(Runner* r, const Node* node, int status) {
    if (node->redirects != NULL)
        return run_redirected(r, node, status);
    if (node->type == NODE_PIPE)
        return run_pipe(r, node, status);

    switch (node->type) {
    case NODE_PIPE:
        return 0;

    case NODE_CASE:
        return run_case(r, node, status);

    case NODE_GROUP:
        return run_list(r, &node->second, status);

    case NODE_SUBSHELL:
        return run_subshell(r, node, status);

    case NODE_FUNCTION:
        return define_function((Node*)node) == 0 ? 0 : 1;

    case NODE_BACKGROUND:
        return run_background(r, node, status);
    case NODE_SIMPLE:
        return run_simple(r, node, status);

    case NODE_IF: {
        int condition;

        errSuppressed++;
        condition = run_list(r, &node->first, status);
        errSuppressed--;

        if (*r->exitShell || pendingLevels > 0)
            return condition;
        if (condition == 0)
            return run_list(r, &node->second, 0);
        if (node->hasElse)
            return run_list(r, &node->third, 0);
        return 0;
    }

    case NODE_WHILE: {
        int result = 0;

        activeLoopDepth++;
        while (!*r->exitShell) {
            int condition;

            if (interrupt_ends_loop(r, result)) {
                result = 130;
                break;
            }
            errSuppressed++;
            condition = run_list(r, &node->first, result);
            errSuppressed--;
            if (*r->exitShell || pendingLevels > 0) {
                if (after_round())
                    break;
                continue;
            }
            if ((condition == 0) == (node->until != 0))
                break;
            result = run_list(r, &node->second, result);
            if (after_round())
                break;
        }
        activeLoopDepth--;
        return result;
    }

    case NODE_FOR: {
        int result = 0;
        char** values = NULL;
        size_t count = 0, i;
        Pipeline pipeline;
        char* command;

        // the words: expanded like the arguments of a command (variables, substitutions, wildcards)
        command = malloc((node->text != NULL ? strlen(node->text) : 64) + 16);
        if (command == NULL)
            return 1;
        if (node->text != NULL) {
            sprintf(command, "x %s", node->text);
        } else {
            // `for NAME; do` goes over the parameters, each one a word of its own
            sprintf(command, "x \"$@\"");
        }
        if (winshell_parse(command, &pipeline) != 0) {
            free(command);
            return 2;
        }
        free(command);
        if (pipeline.count > 0 && winshell_glob(&pipeline) == 0 && pipeline.commands[0].argv != NULL) {
            char* const* argv = pipeline.commands[0].argv;

            for (count = 0; argv[count + 1] != NULL; count++)
                ;
            values = calloc(count + 1, sizeof *values);
            if (values != NULL)
                for (i = 0; i < count; i++)
                    values[i] = _strdup(argv[i + 1]);
        }
        winshell_freePipeline(&pipeline);

        activeLoopDepth++;
        for (i = 0; i < count && values != NULL && !*r->exitShell; i++) {
            if (interrupt_ends_loop(r, result)) {
                result = 130;
                break;
            }
            var_set(node->name, values[i]);
            result = run_list(r, &node->second, result);
            if (after_round())
                break;
        }
        activeLoopDepth--;
        for (i = 0; i < count && values != NULL; i++)
            free(values[i]);
        free(values);
        return result;
    }
    }
    return 0;
}

static int run_list(Runner* r, const List* list, int status) {
    size_t i;

    for (i = 0; i < list->count && !*r->exitShell && pendingLevels == 0; i++) {
        int op = list->ops[i];
        int run = op == LIST_FIRST || op == LIST_SEQ || (op == LIST_AND && status == 0) ||
                  (op == LIST_OR && status != 0);

        if (!run)
            continue;
        winshell_setLastStatus(status);
        status = run_node(r, list->nodes[i], status);
        winshell_setLastStatus(status);
        service_interrupt(r, &status);
        // ERR: a command failed, unless its status is being tested (a condition, the left of && or ||)
        if (status != 0 && trapAction[TRAP_ERR] != NULL && errSuppressed == 0 && !*r->exitShell &&
            pendingLevels == 0 && (list->nodes[i]->type == NODE_SIMPLE || list->nodes[i]->type == NODE_PIPE) &&
            (i + 1 >= list->count || (list->ops[i + 1] != LIST_AND && list->ops[i + 1] != LIST_OR)))
            run_trap(TRAP_ERR, r->exitShell, status);
    }
    return status;
}

/* ---- entry points ------------------------------------------------------------------------ */

/**
 * One complete command of the top level: the and-or lists up to the next newline (or the end of the text),
 * separated by ;. It is parsed whole before any of it runs, so a syntax error in it runs none of it.
 * Returns 0 and fills *list (empty at the end of the text), or -1 with the error in the parser.
 */
static int parse_complete(Parser* p, List* list) {
    int op = LIST_FIRST;
    int afterCommand = 0;               // a command was read since the start or the last ;

    // empty lines and comments in front
    for (;;) {
        skip_blanks(p);
        if (p->text[p->pos] == '#')
            while (p->text[p->pos] != '\0' && p->text[p->pos] != '\n')
                p->pos++;
        if (p->text[p->pos] == '\n')
            p->pos++;
        else
            break;
    }

    for (;;) {
        char c;

        skip_blanks(p);
        c = p->text[p->pos];
        if (c == '#') {
            while (p->text[p->pos] != '\0' && p->text[p->pos] != '\n')
                p->pos++;
            continue;
        }
        if (c == '\0')
            return 0;
        if (c == '\n') {
            p->pos++;
            return 0;
        }
        if (c == ';') {
            if (!afterCommand)
                return syntax(p, "Syntax error near unexpected token '%s'", ";");
            afterCommand = 0;
            p->pos++;
            continue;
        }
        if (parse_andor(p, list, op) != 0)
            return -1;
        op = LIST_SEQ;
        afterCommand = 1;
    }
}

int command_run_text(const char* text, int* exitShell, int status) {
    Parser p;
    Runner runner;

    memset(&p, 0, sizeof p);
    p.text = text;
    runner.exitShell = exitShell;
    runner.loopDepth = 0;

    while (!*exitShell && p.text[p.pos] != '\0') {
        List list;

        memset(&list, 0, sizeof list);
        if (parse_complete(&p, &list) != 0) {
            if (p.error == ERROR_MEMORY)
                fprintf(stderr, "Error: Out of memory\n");
            else
                fprintf(stderr, "Error: %s\n", p.message);
            free_list(&list);
            winshell_setLastStatus(2);
            return 2;
        }
        // a break or continue outside of a loop is refused by command_break: nothing is pending here
        pendingLevels = 0;
        status = run_list(&runner, &list, status);
        free_list(&list);
    }
    if (!*exitShell)
        InterlockedExchange(&interruptRequested, 0);
    return status;
}

int command_incomplete(const char* text) {
    Parser p;

    memset(&p, 0, sizeof p);
    p.text = text;
    p.quiet = 1;
    while (p.text[p.pos] != '\0') {
        List list;
        int result;

        memset(&list, 0, sizeof list);
        result = parse_complete(&p, &list);
        free_list(&list);
        if (result != 0)
            return p.error == ERROR_INCOMPLETE;     // a syntax error is not cured by waiting
    }
    return 0;
}
