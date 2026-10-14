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
#include "command.h"
#include "executer.h"
#include "interp.h"
#include "parser.h"
#include "variables.h"
#include "vec.h"

/* ---- the tree ---------------------------------------------------------------------------- */

typedef struct Node Node;

typedef struct {
    Node** nodes;
    int*   ops;                 // LIST_FIRST for the first, then LIST_SEQ, LIST_AND, LIST_OR
    size_t count;
} List;

typedef enum { NODE_SIMPLE, NODE_IF, NODE_WHILE, NODE_FOR, NODE_PIPE } NodeType;

struct Node {
    NodeType type;
    char*    text;              // NODE_SIMPLE: the command; NODE_FOR: the words after `in` (NULL: "$@")
    char*    redirects;         // a compound command: the redirections written after it (`done < file`), or NULL
    char*    name;              // NODE_FOR: the variable
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
    static const char* const words[] = { "if", "then", "elif", "else", "fi", "while", "until", "do", "done", "for", "in" };
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
        if (c == '|' && text[i + 1] != '|') {
            // a pipe into a compound command (`cmd | while read x; do ...; done`) ends the simple command
            size_t j = i + 1;
            char word[16];
            size_t wordEnd;

            while (is_blank(text[j]) || text[j] == '\n')
                j++;
            if (plain_word(p, j, word, sizeof word, &wordEnd) &&
                (strcmp(word, "while") == 0 || strcmp(word, "until") == 0 || strcmp(word, "for") == 0 ||
                 strcmp(word, "if") == 0)) {
                p->pipeToCompound = 1;
                return i;
            }
        }
        op = winshell_operator_at(text, i, &length, &skip);
        if (op < 0)
            return syntax(p, "Syntax error: '&' is not supported (no background commands)%s", NULL), (size_t)-1;
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
        if (parse_andor(p, list, op) != 0)
            return -1;
        op = LIST_SEQ;
    }
}

/** A command, then any number of `&& command` and `|| command`, to a ; newline or the end. */
static int parse_andor(Parser* p, List* list, int firstOp) {
    int op = firstOp;

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
    if (c == '\0' || c == '\n' || c == ';' || c == '#' || (c == '&' && next == '&') || (c == '|' && next == '|'))
        return 0;
    if (c == '|')
        return syntax(p, "Syntax error: the output of a compound command cannot be piped%s", NULL);
    if (c == '<' || c == '>' || (c == '&' && next == '>') || (isdigit((unsigned char)c) && next == '>')) {
        size_t start = p->pos, end = scan_simple(p, start);

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

static Node* parse_command(Parser* p) {
    char buffer[16];
    const char* word;
    Node* node;

    skip_blanks(p);
    if (p->text[p->pos] == '\0' || p->text[p->pos] == '\n') {
        incomplete(p, "Syntax error: missing command");
        return NULL;
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
        status = type == NODE_IF ? parse_if(p, node) : (type == NODE_WHILE ? parse_while(p, node) : parse_for(p, node));
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

        if (p->pipeToCompound) {
            // left | compound: the left part runs, its output is the input of the compound command
            Node* pipe;
            Node* right;

            p->pipeToCompound = 0;
            p->pos++;                       // the |
            while (is_blank(p->text[p->pos]) || p->text[p->pos] == '\n')
                p->pos++;
            right = parse_command(p);
            if (right == NULL) {
                free_node(node);
                return NULL;
            }
            pipe = new_node(NODE_PIPE);
            if (pipe == NULL) {
                free_node(node);
                free_node(right);
                p->error = ERROR_MEMORY;
                return NULL;
            }
            pipe->type = NODE_PIPE;
            if (list_add(&pipe->first, node, LIST_FIRST) != 0) {
                free_node(node);
                free_node(right);
                free_node(pipe);
                p->error = ERROR_MEMORY;
                return NULL;
            }
            if (list_add(&pipe->second, right, LIST_FIRST) != 0) {
                free_node(right);
                free_node(pipe);            // frees the left part with it
                p->error = ERROR_MEMORY;
                return NULL;
            }
            return pipe;
        }
        return node;
    }
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

/** left | compound: the output of the left part (run to the end, in a subshell) is the input of the compound. */
static int run_pipe_into(Runner* r, const Node* node, int status) {
    char* output = NULL;
    size_t length = 0;
    int leftStatus, result;
    InputRedirection* input;

    if (interp_capture(node->first.nodes[0]->text, &output, &length, &leftStatus) != 0)
        return 1;
    input = interp_input_from_bytes(output, length);
    free(output);
    if (input == NULL) {
        fprintf(stderr, "Error: Cannot make the input of the command\n");
        return 1;
    }
    interp_input_redirected(1);
    result = run_node(r, node->second.nodes[0], status);
    interp_input_redirected(-1);
    interp_input_restore(input);
    return result;
}

static int run_node(Runner* r, const Node* node, int status) {
    if (node->redirects != NULL)
        return run_redirected(r, node, status);
    if (node->type == NODE_PIPE)
        return run_pipe_into(r, node, status);

    switch (node->type) {
    case NODE_PIPE:
        return 0;
    case NODE_SIMPLE:
        return interp_run_simple(node->text, r->exitShell, status);

    case NODE_IF: {
        int condition = run_list(r, &node->first, status);

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

            if (interrupted_now()) {
                result = 130;
                break;
            }
            condition = run_list(r, &node->first, result);
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
            const char* number = var_get("#");
            int n = number != NULL ? atoi(number) : 0, k;
            size_t used = (size_t)sprintf(command, "x");

            for (k = 1; k <= n && k <= 9; k++)
                used += (size_t)sprintf(command + used, " \"$%d\"", k);
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
            if (interrupted_now()) {
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
