/*
 * [[ expression ]] and (( arithmetic )): the tests of bash that are not programs.
 *
 * [[ ]] has its own rules: no word splitting or wildcards in the words, == and != take a pattern at the right (unless it
 * is quoted), =~ takes a regular expression (the parts that matched go to BASH_REMATCH), && || ! and ( ) combine, and the
 * operators of test (-n -z -e -f -d -s -r -w -x -L -h -v, -eq -ne -lt -gt -le -ge, -nt -ot -ef) are there as well.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "arith.h"
#include "command.h"
#include "parser.h"
#include "regex.h"
#include "variables.h"
#include "vpath.h"

typedef struct {
    char** words;
    size_t count, capacity;
    size_t at;
    int    error;               // a syntax error or a failure was reported
} Expression;

static int add_token(Expression* e, const char* start, size_t length) {
    char* copy;

    if (e->count == e->capacity) {
        size_t capacity = e->capacity != 0 ? e->capacity * 2 : 16;
        char** grown = realloc(e->words, capacity * sizeof *grown);

        if (grown == NULL)
            return -1;
        e->words = grown;
        e->capacity = capacity;
    }
    copy = malloc(length + 1);
    if (copy == NULL)
        return -1;
    memcpy(copy, start, length);
    copy[length] = '\0';
    e->words[e->count++] = copy;
    return 0;
}

/** Splits the text into words and the operators ( ) && ||. A word keeps its quotes; the word after =~ runs to the next blank. */
static int tokenize(Expression* e, const char* text) {
    const char* p = text;

    while (*p != '\0') {
        const char* start;
        int depth = 0;
        int regex;

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        if (*p == '\0')
            break;
        regex = e->count > 0 && strcmp(e->words[e->count - 1], "=~") == 0;
        if (!regex && (*p == '(' || *p == ')')) {
            if (add_token(e, p, 1) != 0)
                return -1;
            p++;
            continue;
        }
        if (!regex && ((p[0] == '&' && p[1] == '&') || (p[0] == '|' && p[1] == '|'))) {
            if (add_token(e, p, 2) != 0)
                return -1;
            p += 2;
            continue;
        }
        start = p;
        while (*p != '\0') {
            char c = *p;

            if (c == '\\' && p[1] != '\0') {
                p += 2;
                continue;
            }
            if (c == '\'') {
                p++;
                while (*p != '\0' && *p != '\'')
                    p++;
                if (*p == '\'')
                    p++;
                continue;
            }
            if (c == '"') {
                p++;
                while (*p != '\0' && *p != '"') {
                    if (*p == '\\' && p[1] != '\0')
                        p++;
                    p++;
                }
                if (*p == '"')
                    p++;
                continue;
            }
            if (c == '$' && (p[1] == '(' || p[1] == '{')) {
                size_t n = p[1] == '(' ? winshell_paren_end(p + 1) : 0;

                if (n > 0) {
                    p += 1 + n;
                    continue;
                }
                if (p[1] == '{') {
                    while (*p != '\0' && *p != '}')
                        p++;
                    if (*p == '}')
                        p++;
                    continue;
                }
            }
            if (regex) {
                if (c == '(')
                    depth++;
                else if (c == ')' && depth > 0)
                    depth--;
                else if ((c == ' ' || c == '\t' || c == '\n') && depth == 0)
                    break;
            } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(' || c == ')') {
                break;
            } else if ((c == '&' && p[1] == '&') || (c == '|' && p[1] == '|')) {
                break;
            }
            p++;
        }
        if (p == start)
            p++;                        // a stray character: a word of its own
        if (add_token(e, start, (size_t)(p - start)) != 0)
            return -1;
    }
    return 0;
}

static void free_tokens(Expression* e) {
    size_t i;

    for (i = 0; i < e->count; i++)
        free(e->words[i]);
    free(e->words);
}

static int syntax_error(Expression* e, const char* what) {
    if (!e->error)
        fprintf(stderr, "[[: syntax error: %s\n", what);
    e->error = 1;
    return 0;
}

static const char* peek(const Expression* e) {
    return e->at < e->count ? e->words[e->at] : NULL;
}

static int is_quoted(const char* raw) {
    return strpbrk(raw, "'\"\\") != NULL;
}

/** The word with its expansions done (and its quotes taken off). Free the result. */
static char* value_of(Expression* e, const char* raw) {
    char* value;

    if (strcmp(raw, "\"\"") == 0 || strcmp(raw, "''") == 0)
        return _strdup("");
    value = winshell_expand_text(raw);
    if (value == NULL) {
        e->error = 1;
        return NULL;
    }
    return value;
}

static int is_binary(const char* t) {
    static const char* const ops[] = { "==", "=", "!=", "<", ">", "=~", "-eq", "-ne", "-lt", "-gt", "-le", "-ge", "-nt", "-ot", "-ef", NULL };
    int i;

    for (i = 0; ops[i] != NULL; i++)
        if (strcmp(t, ops[i]) == 0)
            return 1;
    return 0;
}

static int is_unary(const char* t) {
    return t[0] == '-' && t[1] != '\0' && t[2] == '\0' && strchr("nzefdsrwxLhv", t[1]) != NULL;
}

static int is_program_name(const char* name) {
    size_t n = strlen(name);

    return n > 4 && (_stricmp(name + n - 4, ".exe") == 0 || _stricmp(name + n - 4, ".com") == 0 ||
                     _stricmp(name + n - 4, ".bat") == 0 || _stricmp(name + n - 4, ".cmd") == 0);
}

static int file_test(char op, const char* path) {
    char native[VPATH_MAX];
    WIN32_FILE_ATTRIBUTE_DATA data;

    if (op == 'v')
        return var_get(path) != NULL;
    if (path[0] == '\0' || vpath_to_native(path, native, sizeof native) != 0)
        return 0;
    if (!GetFileAttributesExA(native, GetFileExInfoStandard, &data))
        return 0;
    switch (op) {
    case 'e': return 1;
    case 'f': return !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    case 'd': return (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    case 's': return (((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow) != 0 ||
                     (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    case 'r': return 1;
    case 'w': return !(data.dwFileAttributes & FILE_ATTRIBUTE_READONLY);
    case 'x': return (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || is_program_name(path);
    case 'L':
    case 'h': return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    default: return 0;
    }
}

static unsigned long long file_time(const char* path, int* ok) {
    char native[VPATH_MAX];
    WIN32_FILE_ATTRIBUTE_DATA data;

    *ok = 0;
    if (vpath_to_native(path, native, sizeof native) != 0 || !GetFileAttributesExA(native, GetFileExInfoStandard, &data))
        return 0;
    *ok = 1;
    return ((unsigned long long)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
}

static int number_of(Expression* e, const char* text, long long* value) {
    char error[128];

    if (arith_eval(text, value, error, sizeof error) != 0) {
        fprintf(stderr, "[[: %s: %s\n", text, error);
        e->error = 1;
        return -1;
    }
    return 0;
}

/** =~: the regular expression (extended) searched in the text; BASH_REMATCH gets the match and its groups. */
static int regex_match(Expression* e, const char* text, const char* pattern, int quoted) {
    char error[256];
    const char* patterns[1];
    Regex* regex;
    RegexInput input;
    size_t start, end, groups, i;
    size_t* spans;
    int found;

    patterns[0] = pattern;
    regex = regex_compile(patterns, 1, (quoted ? REGEX_FIXED : REGEX_EXTENDED) | REGEX_GROUPS, error, sizeof error);
    if (regex == NULL) {
        fprintf(stderr, "[[: invalid regular expression '%s': %s\n", pattern, error);
        e->error = 1;
        return 2;
    }
    memset(&input, 0, sizeof input);
    if (regex_input_set(&input, (const unsigned char*)text, strlen(text)) != 0) {
        regex_free(regex);
        e->error = 1;
        return 2;
    }
    found = regex_search(regex, &input, 0, &start, &end);
    if (found) {
        char** values;

        groups = regex_group_count(regex);
        spans = calloc(2 * (groups + 1), sizeof *spans);
        values = calloc(groups + 2, sizeof *values);
        if (spans != NULL && values != NULL) {
            spans[0] = start;
            spans[1] = end;
            if (groups > 0)
                regex_groups(regex, &input, start, end, spans);
            for (i = 0; i <= groups; i++) {
                size_t from = spans[2 * i], to = spans[2 * i + 1];

                if (from == (size_t)-1 || to == (size_t)-1) {
                    values[i] = _strdup("");
                } else {
                    size_t a = input.offset[from], b = input.offset[to];

                    values[i] = malloc(b - a + 1);
                    if (values[i] != NULL) {
                        memcpy(values[i], text + a, b - a);
                        values[i][b - a] = '\0';
                    }
                }
            }
            var_array_assign("BASH_REMATCH", values, groups + 1);
            for (i = 0; i <= groups; i++)
                free(values[i]);
        }
        free(spans);
        free(values);
    } else {
        var_array_assign("BASH_REMATCH", NULL, 0);
    }
    regex_input_free(&input);
    regex_free(regex);
    return found ? 1 : 0;
}

static int parse_or(Expression* e, int live);

static int parse_primary(Expression* e, int live) {
    const char* t = peek(e);

    if (t == NULL)
        return syntax_error(e, "unexpected end of the expression");
    if (strcmp(t, "(") == 0) {
        int value;

        e->at++;
        value = parse_or(e, live);
        if (e->error)
            return 0;
        if (peek(e) == NULL || strcmp(peek(e), ")") != 0)
            return syntax_error(e, "missing ')'");
        e->at++;
        return value;
    }
    if (is_unary(t) && e->at + 1 < e->count && strcmp(e->words[e->at + 1], "&&") != 0 && strcmp(e->words[e->at + 1], "||") != 0 &&
        strcmp(e->words[e->at + 1], ")") != 0 && !(is_binary(e->words[e->at + 1]) && e->at + 2 < e->count)) {
        char op = t[1];
        char* operand;
        int result = 0;

        e->at++;
        if (live) {
            operand = value_of(e, e->words[e->at]);
            if (operand != NULL) {
                if (op == 'n')
                    result = operand[0] != '\0';
                else if (op == 'z')
                    result = operand[0] == '\0';
                else
                    result = file_test(op, operand);
                free(operand);
            }
        }
        e->at++;
        return result;
    }
    // a word, perhaps with a binary operator and a second word
    {
        const char* left = t;
        const char* op = e->at + 1 < e->count ? e->words[e->at + 1] : NULL;

        if (op != NULL && is_binary(op) && e->at + 2 < e->count) {
            const char* right = e->words[e->at + 2];
            int result = 0;

            e->at += 3;
            if (live) {
                char* l = value_of(e, left);
                char* r = value_of(e, right);

                if (l != NULL && r != NULL) {
                    if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0 || strcmp(op, "!=") == 0) {
                        int same = is_quoted(right) ? strcmp(l, r) == 0 : winshell_pattern_matches(r, l) != 0;

                        result = strcmp(op, "!=") == 0 ? !same : same;
                    } else if (strcmp(op, "<") == 0) {
                        result = strcmp(l, r) < 0;
                    } else if (strcmp(op, ">") == 0) {
                        result = strcmp(l, r) > 0;
                    } else if (strcmp(op, "=~") == 0) {
                        result = regex_match(e, l, r, right[0] == '"' || right[0] == '\'') == 1;
                    } else if (strcmp(op, "-nt") == 0 || strcmp(op, "-ot") == 0 || strcmp(op, "-ef") == 0) {
                        int okL, okR;
                        unsigned long long tl = file_time(l, &okL), tr = file_time(r, &okR);

                        if (strcmp(op, "-nt") == 0)
                            result = okL && (!okR || tl > tr);
                        else if (strcmp(op, "-ot") == 0)
                            result = okR && (!okL || tl < tr);
                        else
                            result = okL && okR && strcmp(l, r) == 0;
                    } else {
                        long long a, b;

                        if (number_of(e, l, &a) == 0 && number_of(e, r, &b) == 0) {
                            switch (op[1]) {
                            case 'e': result = a == b; break;
                            case 'n': result = a != b; break;
                            case 'l': result = op[2] == 't' ? a < b : a <= b; break;
                            default: result = op[2] == 't' ? a > b : a >= b; break;
                            }
                        }
                    }
                }
                free(l);
                free(r);
            }
            return result;
        }
        e->at++;
        if (!live)
            return 0;
        {
            char* v = value_of(e, left);
            int result = v != NULL && v[0] != '\0';

            free(v);
            return result;
        }
    }
}

static int parse_not(Expression* e, int live) {
    const char* t = peek(e);

    if (t != NULL && strcmp(t, "!") == 0) {
        int value;

        e->at++;
        value = parse_not(e, live);
        return !value;
    }
    return parse_primary(e, live);
}

static int parse_and(Expression* e, int live) {
    int value = parse_not(e, live);

    while (!e->error && peek(e) != NULL && strcmp(peek(e), "&&") == 0) {
        int right;

        e->at++;
        right = parse_not(e, live && value);
        value = value && right;
    }
    return value;
}

static int parse_or(Expression* e, int live) {
    int value = parse_and(e, live);

    while (!e->error && peek(e) != NULL && strcmp(peek(e), "||") == 0) {
        int right;

        e->at++;
        right = parse_and(e, live && !value);
        value = value || right;
    }
    return value;
}

int command_dbracket(const char* text) {
    Expression e;
    int value;

    memset(&e, 0, sizeof e);
    if (tokenize(&e, text) != 0) {
        free_tokens(&e);
        return 2;
    }
    if (e.count == 0) {
        free_tokens(&e);
        return 2;
    }
    value = parse_or(&e, 1);
    if (!e.error && e.at < e.count)
        syntax_error(&e, "unexpected word in the expression");
    free_tokens(&e);
    if (e.error)
        return 2;
    return value ? 0 : 1;
}

int command_arithmetic(const char* text) {
    // $(( )) knows what to do with < and > and with the variables: the text goes through it
    char* wrapped = malloc(strlen(text) + 8);
    char* expanded;
    long long value;

    if (wrapped == NULL)
        return 1;
    sprintf(wrapped, "$((%s))", text);
    expanded = winshell_expand_text(wrapped);
    free(wrapped);
    if (expanded == NULL)
        return 1;
    value = strtoll(expanded, NULL, 10);
    free(expanded);
    return value != 0 ? 0 : 1;
}
