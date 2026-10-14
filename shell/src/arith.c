/*
 * Arithmetic expansion: $(( expression )). 64-bit integers; the operators of C (except ++ -- and sizeof) and **:
 *
 *   ,   = += -= *= /= %= <<= >>= &= ^= |=   ?:   ||   &&   |   ^   &   == !=   < <= > >=   << >>   + -
 *   * / %   unary + - ! ~   **   ( )
 *
 * Numbers are decimal, 0x hex or 0 octal; a name is the variable of that name (unset or empty is 0, a value
 * that is an expression is evaluated). Dividing by zero is an error. The result of a branch that is not taken
 * is not computed, so it has no effect.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arith.h"
#include "variables.h"

typedef struct {
    const char* text;
    size_t      pos;
    int         depth;              // of variables whose value is an expression
    char*       error;
    size_t      errorSize;
    int         failed;
} Arith;

static long long expression(Arith* a, int live);

static int fail(Arith* a, const char* format, const char* detail) {
    if (!a->failed) {
        a->failed = 1;
        snprintf(a->error, a->errorSize, format, detail != NULL ? detail : "");
    }
    return 0;
}

static void skip_blanks(Arith* a) {
    while (isspace((unsigned char)a->text[a->pos]))
        a->pos++;
}

static int accept(Arith* a, const char* op) {
    size_t n = strlen(op);

    skip_blanks(a);
    if (strncmp(a->text + a->pos, op, n) != 0)
        return 0;
    a->pos += n;
    return 1;
}

/** Whether the operator is next and is not the start of a longer one: `|` and not `||` or `|=`. */
static int peek_op(Arith* a, const char* op, const char* notFollowedBy) {
    size_t n = strlen(op);

    skip_blanks(a);
    if (strncmp(a->text + a->pos, op, n) != 0)
        return 0;
    return strchr(notFollowedBy, a->text[a->pos + n]) == NULL || a->text[a->pos + n] == '\0';
}

static int is_name_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static long long value_of_text(Arith* a, const char* text, int live);

static long long value_of_name(Arith* a, const char* name, int live) {
    const char* value = var_get(name);

    if (value == NULL || value[0] == '\0')
        return 0;
    return value_of_text(a, value, live);
}

/** The value of the text of a variable: a number, or an expression. */
static long long value_of_text(Arith* a, const char* text, int live) {
    char* end;
    long long number;

    while (isspace((unsigned char)*text))
        text++;
    number = strtoll(text, &end, 0);
    while (isspace((unsigned char)*end))
        end++;
    if (end != text && *end == '\0')
        return number;
    if (a->depth >= 8) {
        fail(a, "%s: expression recursion level exceeded", text);
        return 0;
    }
    {
        Arith inner;
        long long result;

        inner = *a;
        inner.text = text;
        inner.pos = 0;
        inner.depth = a->depth + 1;
        result = expression(&inner, live);
        if (!inner.failed && inner.text[inner.pos] != '\0' && !isspace((unsigned char)inner.text[inner.pos]))
            fail(&inner, "%s: syntax error in expression", text);
        if (inner.failed)
            a->failed = 1;
        return result;
    }
}

static long long power(long long base, long long exponent, Arith* a) {
    long long result = 1;

    if (exponent < 0) {
        fail(a, "exponent less than 0%s", NULL);
        return 0;
    }
    while (exponent-- > 0)
        result *= base;
    return result;
}

static long long primary(Arith* a, int live) {
    skip_blanks(a);
    if (a->text[a->pos] == '(') {
        long long value;

        a->pos++;
        value = expression(a, live);
        if (!accept(a, ")"))
            return fail(a, "missing ')' in the expression%s", NULL);
        return value;
    }
    if (isdigit((unsigned char)a->text[a->pos])) {
        char* end;
        long long value = strtoll(a->text + a->pos, &end, 0);

        if (end == a->text + a->pos || isalnum((unsigned char)*end) || *end == '_') {
            char token[40];
            size_t n = 0;

            while (isalnum((unsigned char)a->text[a->pos + n]) && n < sizeof token - 1) {
                token[n] = a->text[a->pos + n];
                n++;
            }
            token[n] = '\0';
            return fail(a, "%s: value too great for base or invalid number", token);
        }
        a->pos = (size_t)(end - a->text);
        return value;
    }
    if (is_name_start(a->text[a->pos])) {
        char name[128];
        size_t n = 0;

        while ((isalnum((unsigned char)a->text[a->pos + n]) || a->text[a->pos + n] == '_') && n < sizeof name - 1) {
            name[n] = a->text[a->pos + n];
            n++;
        }
        name[n] = '\0';
        a->pos += n;
        return live ? value_of_name(a, name, live) : 0;
    }
    if (a->text[a->pos] == '\0')
        return fail(a, "syntax error: operand expected%s", NULL);
    {
        char token[2] = { a->text[a->pos], '\0' };
        return fail(a, "syntax error: unexpected '%s'", token);
    }
}

static long long unary(Arith* a, int live) {
    skip_blanks(a);
    if (a->text[a->pos] == '+' && a->text[a->pos + 1] != '+') {
        a->pos++;
        return unary(a, live);
    }
    if (a->text[a->pos] == '-' && a->text[a->pos + 1] != '-') {
        a->pos++;
        return -unary(a, live);
    }
    if (a->text[a->pos] == '!') {
        a->pos++;
        return !unary(a, live);
    }
    if (a->text[a->pos] == '~') {
        a->pos++;
        return ~unary(a, live);
    }
    {
        long long base = primary(a, live);

        if (peek_op(a, "**", "=")) {
            long long exponent;

            a->pos += 2;
            exponent = unary(a, live);          // right associative
            return live ? power(base, exponent, a) : 0;
        }
        return base;
    }
}

static long long multiplicative(Arith* a, int live) {
    long long value = unary(a, live);

    for (;;) {
        char op;
        long long right;

        skip_blanks(a);
        op = a->text[a->pos];
        if ((op != '*' && op != '/' && op != '%') || a->text[a->pos + 1] == '=' ||
            (op == '*' && a->text[a->pos + 1] == '*'))
            return value;
        a->pos++;
        right = unary(a, live);
        if (op == '*') {
            value = value * right;
        } else if (right == 0) {
            if (live)
                fail(a, "division by 0%s", NULL);
            value = 0;
        } else if (op == '/') {
            value = value / right;
        } else {
            value = value % right;
        }
    }
}

static long long additive(Arith* a, int live) {
    long long value = multiplicative(a, live);

    for (;;) {
        skip_blanks(a);
        if (peek_op(a, "+", "+=")) {
            a->pos++;
            value += multiplicative(a, live);
        } else if (peek_op(a, "-", "-=")) {
            a->pos++;
            value -= multiplicative(a, live);
        } else {
            return value;
        }
    }
}

static long long shift(Arith* a, int live) {
    long long value = additive(a, live);

    for (;;) {
        if (peek_op(a, "<<", "=")) {
            a->pos += 2;
            value = value << (additive(a, live) & 63);
        } else if (peek_op(a, ">>", "=")) {
            a->pos += 2;
            value = value >> (additive(a, live) & 63);
        } else {
            return value;
        }
    }
}

static long long relational(Arith* a, int live) {
    long long value = shift(a, live);

    for (;;) {
        if (peek_op(a, "<=", "")) {
            a->pos += 2;
            value = value <= shift(a, live);
        } else if (peek_op(a, ">=", "")) {
            a->pos += 2;
            value = value >= shift(a, live);
        } else if (peek_op(a, "<", "<=")) {
            a->pos++;
            value = value < shift(a, live);
        } else if (peek_op(a, ">", ">=")) {
            a->pos++;
            value = value > shift(a, live);
        } else {
            return value;
        }
    }
}

static long long equality(Arith* a, int live) {
    long long value = relational(a, live);

    for (;;) {
        if (peek_op(a, "==", "")) {
            a->pos += 2;
            value = value == relational(a, live);
        } else if (peek_op(a, "!=", "")) {
            a->pos += 2;
            value = value != relational(a, live);
        } else {
            return value;
        }
    }
}

static long long bit_and(Arith* a, int live) {
    long long value = equality(a, live);

    while (peek_op(a, "&", "&=")) {
        a->pos++;
        value &= equality(a, live);
    }
    return value;
}

static long long bit_xor(Arith* a, int live) {
    long long value = bit_and(a, live);

    while (peek_op(a, "^", "=")) {
        a->pos++;
        value ^= bit_and(a, live);
    }
    return value;
}

static long long bit_or(Arith* a, int live) {
    long long value = bit_xor(a, live);

    while (peek_op(a, "|", "|=")) {
        a->pos++;
        value |= bit_xor(a, live);
    }
    return value;
}

static long long logical_and(Arith* a, int live) {
    long long value = bit_or(a, live);

    while (peek_op(a, "&&", "")) {
        long long right;

        a->pos += 2;
        right = bit_or(a, live && value != 0);
        value = value != 0 && right != 0;
    }
    return value;
}

static long long logical_or(Arith* a, int live) {
    long long value = logical_and(a, live);

    while (peek_op(a, "||", "")) {
        long long right;

        a->pos += 2;
        right = logical_and(a, live && value == 0);
        value = value != 0 || right != 0;
    }
    return value;
}

static long long assignment(Arith* a, int live);

static long long conditional(Arith* a, int live) {
    long long condition = logical_or(a, live);

    if (peek_op(a, "?", "")) {
        long long yes, no;

        a->pos++;
        yes = assignment(a, live && condition != 0);
        if (!accept(a, ":"))
            return fail(a, "syntax error: ':' expected for the conditional expression%s", NULL);
        no = assignment(a, live && condition == 0);
        return condition != 0 ? yes : no;
    }
    return condition;
}

/** NAME op= expression, or a conditional expression. */
static long long assignment(Arith* a, int live) {
    size_t start;

    skip_blanks(a);
    start = a->pos;
    if (is_name_start(a->text[a->pos])) {
        char name[128];
        size_t n = 0;
        char op[4] = "";
        size_t i;
        static const char* const operators[] = { "<<=", ">>=", "+=", "-=", "*=", "/=", "%=", "&=", "^=", "|=", "=" };

        while ((isalnum((unsigned char)a->text[a->pos + n]) || a->text[a->pos + n] == '_') && n < sizeof name - 1) {
            name[n] = a->text[a->pos + n];
            n++;
        }
        name[n] = '\0';
        a->pos += n;
        skip_blanks(a);
        for (i = 0; i < sizeof operators / sizeof operators[0]; i++) {
            size_t length = strlen(operators[i]);

            if (strncmp(a->text + a->pos, operators[i], length) == 0 &&
                !(length == 1 && a->text[a->pos + 1] == '=')) {
                snprintf(op, sizeof op, "%s", operators[i]);
                a->pos += length;
                break;
            }
        }
        if (op[0] != '\0') {
            long long right = assignment(a, live);
            long long value = right;
            char text[32];

            if (live && strcmp(op, "=") != 0) {
                long long current = value_of_name(a, name, live);

                switch (op[0]) {
                case '+': value = current + right; break;
                case '-': value = current - right; break;
                case '*': value = current * right; break;
                case '/':
                case '%':
                    if (right == 0) {
                        fail(a, "division by 0%s", NULL);
                        return 0;
                    }
                    value = op[0] == '/' ? current / right : current % right;
                    break;
                case '&': value = current & right; break;
                case '^': value = current ^ right; break;
                case '|': value = current | right; break;
                case '<': value = current << (right & 63); break;
                default:  value = current >> (right & 63); break;
                }
            }
            if (live) {
                snprintf(text, sizeof text, "%lld", value);
                var_set(name, text);
            }
            return value;
        }
        a->pos = start;                 // not an assignment: it is a variable in an expression
    }
    return conditional(a, live);
}

static long long expression(Arith* a, int live) {
    long long value = assignment(a, live);

    while (!a->failed && peek_op(a, ",", "")) {
        a->pos++;
        value = assignment(a, live);
    }
    return value;
}

int arith_eval(const char* text, long long* result, char* error, size_t errorSize) {
    Arith a;

    memset(&a, 0, sizeof a);
    a.text = text;
    a.error = error;
    a.errorSize = errorSize;
    skip_blanks(&a);
    if (a.text[a.pos] == '\0') {
        *result = 0;                    // an empty expression is 0
        return 0;
    }
    *result = expression(&a, 1);
    if (!a.failed) {
        skip_blanks(&a);
        if (a.text[a.pos] != '\0') {
            char token[2] = { a.text[a.pos], '\0' };
            fail(&a, "syntax error: unexpected '%s' in the expression", token);
        }
    }
    return a.failed ? -1 : 0;
}
