/*
 * Parsing the expression of find: precedence from low to high is , then -o then -a (also implied by
 * two tests next to each other) then ! then the primaries and ( ... ).
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "find.h"
#include "fsutil.h"
#include "vpath.h"

#define PROGRAM "find"

typedef struct {
    int           argc;
    char**        argv;
    int           pos;
    FindOptions*  options;
    int           failed;
    int           regexFlags;       // of -regextype: 0 emacs (translated), REGEX_EXTENDED, or 'b' for basic
    int           emacs;
} Parser;

static void fail(Parser* p, const char* format, const char* detail) {
    if (!p->failed) {
        fprintf(stderr, PROGRAM ": ");
        fprintf(stderr, format, detail);
        fputc('\n', stderr);
    }
    p->failed = 1;
}

static const char* peek(const Parser* p) {
    return p->pos < p->argc ? p->argv[p->pos] : NULL;
}

static int accept_word(Parser* p, const char* word) {
    if (p->pos < p->argc && strcmp(p->argv[p->pos], word) == 0) {
        p->pos++;
        return 1;
    }
    return 0;
}

static Node* new_node(Parser* p, NodeKind kind) {
    Node* n = calloc(1, sizeof *n);
    if (n == NULL) {
        fail(p, "memory exhausted%s", "");
        exit(1);
    }
    n->kind = kind;
    return n;
}

static Node* make_binary(Parser* p, NodeKind kind, Node* left, Node* right) {
    Node* n = new_node(p, kind);
    n->left = left;
    n->right = right;
    return n;
}

/** The argument of the predicate that was just read, or NULL after a message. */
static const char* argument(Parser* p, const char* name) {
    if (p->pos >= p->argc) {
        fail(p, "missing argument to `%s'", name);
        return NULL;
    }
    return p->argv[p->pos++];
}

static int parse_number(Parser* p, const char* name, const char* text, long long* out, int signedAllowed, int* compare) {
    const char* s = text;
    char* end;

    *compare = 0;
    if (signedAllowed && (*s == '+' || *s == '-')) {
        *compare = *s == '+' ? 1 : -1;
        s++;
    }
    if (*s < '0' || *s > '9') {
        fail(p, "invalid argument `%s' to ", text);
        fprintf(stderr, "`%s'\n", name);
        return -1;
    }
    *out = _strtoi64(s, &end, 10);
    if (*end != '\0') {
        fail(p, "invalid argument `%s' to ", text);
        fprintf(stderr, "`%s'\n", name);
        return -1;
    }
    return 0;
}

/** The emacs syntax of find's -regex, which is the default, as an extended expression. */
static char* emacs_to_extended(const char* s) {
    size_t n = strlen(s);
    char* out = malloc(n * 2 + 1);
    size_t o = 0, i;

    if (out == NULL)
        return NULL;
    for (i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            char c = s[i + 1];
            if (c == '(' || c == ')' || c == '|' || c == '{' || c == '}') {
                out[o++] = c;
            } else {
                out[o++] = '\\';
                out[o++] = c;
            }
            i++;
        } else if (s[i] == '(' || s[i] == ')' || s[i] == '|' || s[i] == '{' || s[i] == '}') {
            out[o++] = '\\';
            out[o++] = s[i];
        } else if (s[i] == '[') {
            // a bracket expression is copied as it is
            size_t k = i + 1;
            if (k < n && s[k] == '^')
                k++;
            if (k < n && s[k] == ']')
                k++;
            while (k < n && s[k] != ']')
                k++;
            while (i <= k && i < n)
                out[o++] = s[i++];
            i--;
        } else {
            out[o++] = s[i];
        }
    }
    out[o] = '\0';
    return out;
}

static int compile_regex(Parser* p, Node* n, const char* pattern, int icase) {
    char error[200];
    const char* patterns[1];
    char* translated = NULL;
    int flags = REGEX_LINE;

    if (p->emacs) {
        translated = emacs_to_extended(pattern);
        patterns[0] = translated;
        flags |= REGEX_EXTENDED;
    } else {
        patterns[0] = pattern;
        if (p->regexFlags == REGEX_EXTENDED)
            flags |= REGEX_EXTENDED;
    }
    if (icase)
        flags |= REGEX_ICASE;
    n->regex = regex_compile(patterns, 1, flags, error, sizeof error);
    free(translated);
    if (n->regex == NULL) {
        fail(p, "Invalid regular expression `%s': ", pattern);
        fprintf(stderr, "%s\n", error);
        return -1;
    }
    return 0;
}

/** A time stamp of a file for -newer. */
static int file_time(Parser* p, const char* path, char which, FILETIME* out) {
    char native[VPATH_MAX];
    WIN32_FILE_ATTRIBUTE_DATA data;

    if (vpath_to_native(path, native, sizeof native) != 0 || !GetFileAttributesExA(native, GetFileExInfoStandard, &data)) {
        fail(p, "`%s': No such file or directory", path);
        return -1;
    }
    *out = which == 'a' ? data.ftLastAccessTime : (which == 'c' ? data.ftCreationTime : data.ftLastWriteTime);
    return 0;
}

static Node* parse_exec(Parser* p, const char* name, int inDirectory, int confirm) {
    Node* n = new_node(p, N_TEST);
    int start = p->pos, end = -1, i;

    n->pred = P_EXEC;
    n->inDirectory = inDirectory;
    n->confirm = confirm;
    for (i = start; i < p->argc; i++) {
        if (strcmp(p->argv[i], ";") == 0) {
            end = i;
            break;
        }
        if (strcmp(p->argv[i], "+") == 0 && i > start && strcmp(p->argv[i - 1], "{}") == 0 && !confirm) {
            end = i;
            n->batch = 1;
            break;
        }
    }
    if (end < 0) {
        fail(p, "missing argument to `%s'", name);
        return NULL;
    }
    if (end == start) {
        fail(p, "missing argument to `%s'", name);
        return NULL;
    }
    n->argv = &p->argv[start];
    n->argc = end - start;
    p->pos = end + 1;
    p->options->hasAction = 1;
    return n;
}

static Node* parse_primary(Parser* p);

static Node* parse_comma(Parser* p);

/** -true -false, the names and paths, regular expressions and the type. */
static int parse_name_test(Parser* p, const char* word, Node* n) {
    const char* a;

#define NEED(name)                                      \
    do {                                                \
        a = argument(p, name);                          \
        if (a == NULL)                                  \
            return -1;                                  \
    } while (0)
#define IS(name) (strcmp(word, name) == 0)

    if (IS("-true")) {
        n->pred = P_TRUE;
    } else if (IS("-false")) {
        n->pred = P_FALSE;
    } else if (IS("-name") || IS("-iname") || IS("-path") || IS("-wholename") || IS("-ipath") || IS("-iwholename")) {
        NEED(word);
        n->pred = IS("-name") ? P_NAME : IS("-iname") ? P_INAME : (IS("-path") || IS("-wholename")) ? P_PATH : P_IPATH;
        n->text = a;
    } else if (IS("-regex") || IS("-iregex")) {
        NEED(word);
        n->pred = P_REGEX;
        if (compile_regex(p, n, a, IS("-iregex")) != 0)
            return -1;
    } else if (IS("-regextype")) {
        NEED(word);
        n->pred = P_OPTION;
        p->emacs = 0;
        p->regexFlags = 0;
        if (strcmp(a, "emacs") == 0)
            p->emacs = 1;
        else if (strcmp(a, "posix-extended") == 0 || strcmp(a, "egrep") == 0 || strcmp(a, "posix-egrep") == 0)
            p->regexFlags = REGEX_EXTENDED;
        else if (strcmp(a, "posix-basic") != 0 && strcmp(a, "grep") != 0) {
            fail(p, "Unknown regular expression type `%s'", a);
            return -1;
        }
    } else if (IS("-type") || IS("-xtype")) {
        size_t i;
        NEED(word);
        n->pred = IS("-type") ? P_TYPE : P_XTYPE;
        n->text = a;
        for (i = 0; a[i] != '\0'; i++) {
            if (strchr("bcdpfls", a[i]) != NULL && (i == 0 || a[i - 1] == ',')) {
                if (a[i + 1] != '\0' && a[i + 1] != ',') {
                    fail(p, "Must separate multiple arguments to -type using: ','%s", "");
                    return -1;
                }
            } else if (a[i] != ',') {
                fail(p, "Unknown argument to -type: %s", a + i);
                return -1;
            }
        }
    } else {
        return 0;
    }
#undef NEED
#undef IS
    return 1;
}

/** The tests of the size, the times and the state of a file. */
static int parse_metadata_test(Parser* p, const char* word, Node* n) {
    const char* a;

#define NEED(name)                                      \
    do {                                                \
        a = argument(p, name);                          \
        if (a == NULL)                                  \
            return -1;                                  \
    } while (0)
#define IS(name) (strcmp(word, name) == 0)

    if (IS("-size")) {
        size_t length;
        char unit = 'b';
        char text[64];
        NEED(word);
        length = strlen(a);
        snprintf(text, sizeof text, "%s", a);
        if (length > 0 && strchr("cwbkMG", text[length - 1]) != NULL && length > 1) {
            unit = text[length - 1];
            text[length - 1] = '\0';
        }
        n->pred = P_SIZE;
        n->unit = unit;
        if (parse_number(p, "-size", text, &n->number, 1, &n->compare) != 0)
            return -1;
    } else if (IS("-empty")) {
        n->pred = P_EMPTY;
    } else if (IS("-newer") || IS("-anewer") || IS("-cnewer")) {
        NEED(word);
        n->pred = P_NEWER;
        n->type = IS("-anewer") ? 'a' : IS("-cnewer") ? 'c' : 'm';
        if (file_time(p, a, 'm', &n->reference) != 0)
            return -1;
    } else if (IS("-mtime") || IS("-atime") || IS("-ctime")) {
        NEED(word);
        n->pred = P_TIME;
        n->type = word[1];
        n->unit = 'd';
        if (parse_number(p, word, a, &n->number, 1, &n->compare) != 0)
            return -1;
    } else if (IS("-mmin") || IS("-amin") || IS("-cmin")) {
        NEED(word);
        n->pred = P_MIN;
        n->type = word[1];
        n->unit = 'm';
        if (parse_number(p, word, a, &n->number, 1, &n->compare) != 0)
            return -1;
    } else if (IS("-readable")) {
        n->pred = P_READABLE;
    } else if (IS("-writable")) {
        n->pred = P_WRITABLE;
    } else if (IS("-executable")) {
        n->pred = P_EXECUTABLE;
    } else {
        return 0;
    }
#undef NEED
#undef IS
    return 1;
}

/** The tests: sets n and returns 1; 0 when the word is not one of them; -1 after a message. */
static int parse_test(Parser* p, const char* word, Node* n) {
    int result = parse_name_test(p, word, n);

    if (result == 0)
        result = parse_metadata_test(p, word, n);
    return result;
}

/** The actions and the global options: sets n and returns 1; 0 when the word is not one of them; -1 after a message. */
static int parse_action(Parser* p, const char* word, Node* n) {
    const char* a;
    FindOptions* o = p->options;

#define NEED(name)                                      \
    do {                                                \
        a = argument(p, name);                          \
        if (a == NULL)                                  \
            return -1;                                  \
    } while (0)
#define IS(name) (strcmp(word, name) == 0)

    if (IS("-prune")) {
        n->pred = P_PRUNE;
    } else if (IS("-quit")) {
        n->pred = P_QUIT;
        o->hasAction = 1;
    } else if (IS("-print")) {
        n->pred = P_PRINT;
        o->hasAction = 1;
    } else if (IS("-print0")) {
        n->pred = P_PRINT0;
        o->hasAction = 1;
    } else if (IS("-printf")) {
        NEED(word);
        n->pred = P_PRINTF;
        n->text = a;
        o->hasAction = 1;
    } else if (IS("-delete")) {
        n->pred = P_DELETE;
        o->depthFirst = 1;
        o->hasAction = 1;
    } else if (IS("-maxdepth") || IS("-mindepth")) {
        long long value;
        int compare;
        NEED(word);
        n->pred = P_OPTION;
        if (parse_number(p, word, a, &value, 0, &compare) != 0)
            return -1;
        if (IS("-maxdepth"))
            o->maxDepth = (int)value;
        else
            o->minDepth = (int)value;
    } else if (IS("-depth") || IS("-d")) {
        n->pred = P_OPTION;
        o->depthFirst = 1;
    } else if (IS("-follow")) {
        n->pred = P_OPTION;
        o->follow = 1;
    } else if (IS("-xdev") || IS("-mount") || IS("-noleaf") || IS("-ignore_readdir_race") ||
               IS("-noignore_readdir_race") || IS("-nowarn") || IS("-warn")) {
        n->pred = P_OPTION;
    } else {
        return 0;
    }
#undef NEED
#undef IS
    return 1;
}

static Node* parse_predicate(Parser* p, const char* word) {
    Node* n;
    int result;

    if (strcmp(word, "-exec") == 0 || strcmp(word, "-execdir") == 0 || strcmp(word, "-ok") == 0 ||
        strcmp(word, "-okdir") == 0)
        return parse_exec(p, word, strcmp(word, "-execdir") == 0 || strcmp(word, "-okdir") == 0,
                          strcmp(word, "-ok") == 0 || strcmp(word, "-okdir") == 0);

    n = new_node(p, N_TEST);
    result = parse_test(p, word, n);
    if (result == 0)
        result = parse_action(p, word, n);
    if (result == 0)
        fail(p, "unknown predicate `%s'", word);
    if (result <= 0) {
        free(n);
        return NULL;
    }
    return n;
}

static Node* parse_primary(Parser* p) {
    const char* word = peek(p);
    Node* n;

    if (word == NULL) {
        fail(p, "invalid expression; expected an expression%s", "");
        return NULL;
    }
    p->pos++;

    if (strcmp(word, "(") == 0) {
        n = parse_comma(p);
        if (n == NULL)
            return NULL;
        if (!accept_word(p, ")")) {
            fail(p, "invalid expression; I was expecting to find a ')' somewhere but did not see one.%s", "");
            return NULL;
        }
        return n;
    }
    if (strcmp(word, "!") == 0 || strcmp(word, "-not") == 0) {
        Node* operand = parse_primary(p);
        if (operand == NULL)
            return NULL;
        n = new_node(p, N_NOT);
        n->left = operand;
        return n;
    }
    if (word[0] != '-' || strcmp(word, "-o") == 0 || strcmp(word, "-a") == 0) {
        fail(p, "paths must precede expression: `%s'", word);
        return NULL;
    }
    return parse_predicate(p, word);
}

static int at_primary(const Parser* p) {
    const char* w = peek(p);
    if (w == NULL)
        return 0;
    return strcmp(w, ")") != 0 && strcmp(w, ",") != 0 && strcmp(w, "-o") != 0 && strcmp(w, "-or") != 0;
}

static Node* parse_and(Parser* p) {
    Node* left = parse_primary(p);
    while (left != NULL && at_primary(p)) {
        Node* right;
        if (accept_word(p, "-a") || accept_word(p, "-and")) {
            if (!at_primary(p)) {
                fail(p, "invalid expression; expected an expression after -a%s", "");
                return NULL;
            }
        }
        right = parse_primary(p);
        if (right == NULL)
            return NULL;
        left = make_binary(p, N_AND, left, right);
    }
    return left;
}

static Node* parse_or(Parser* p) {
    Node* left = parse_and(p);
    while (left != NULL && (accept_word(p, "-o") || accept_word(p, "-or"))) {
        Node* right = parse_and(p);
        if (right == NULL)
            return NULL;
        left = make_binary(p, N_OR, left, right);
    }
    return left;
}

static Node* parse_comma(Parser* p) {
    Node* left = parse_or(p);
    while (left != NULL && accept_word(p, ",")) {
        Node* right = parse_or(p);
        if (right == NULL)
            return NULL;
        left = make_binary(p, N_COMMA, left, right);
    }
    return left;
}

Node* find_parse(int argc, char* argv[], FindOptions* options, int* ok) {
    Parser p;
    Node* n;

    memset(&p, 0, sizeof p);
    p.argc = argc;
    p.argv = argv;
    p.options = options;
    p.emacs = 1;
    *ok = 0;

    if (argc == 0) {
        *ok = 1;
        return NULL;
    }
    n = parse_comma(&p);
    if (n != NULL && p.pos < argc && !p.failed) {
        if (strcmp(argv[p.pos], ")") == 0)
            fail(&p, "unmatched ) %s", "");
        else
            fail(&p, "unexpected extra predicate `%s'", argv[p.pos]);
        n = NULL;
    }
    *ok = n != NULL && !p.failed;
    return *ok ? n : NULL;
}
