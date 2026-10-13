/*
 * expr: evaluates an expression made of the arguments, as GNU expr does.
 *
 * Every argument is a token: an operator only when the whole argument is the operator. All values are
 * strings; an integer is a string of digits with an optional "-", and the arithmetic is done with
 * numbers of any size (like GNU expr, which uses GMP). Precedence, lowest first:
 *
 *   |   &   < <= = == != >= >   + -   * / %   :   ( )
 *
 * Exit status: 0 for a result that is not null (empty, or 0), 1 for a null one, 2 for an invalid
 * expression, 3 for an error while evaluating (a division by zero, a regular expression).
 */

#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "regex.h"
#include "utf8.h"

#define VERSION "0.1"
#define PROGRAM "expr"

#define STATUS_INVALID 2
#define STATUS_FAILURE 3

static void print_usage(void) {
    puts("Usage: expr EXPRESSION\n"
         "   or: expr OPTION\n"
         "\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Print the value of EXPRESSION to standard output. A blank line below separates\n"
         "increasing precedence groups. EXPRESSION may be:\n"
         "\n"
         "  ARG1 | ARG2       ARG1 if it is neither null nor 0, otherwise ARG2\n"
         "\n"
         "  ARG1 & ARG2       ARG1 if neither argument is null or 0, otherwise 0\n"
         "\n"
         "  ARG1 < ARG2       ARG1 is less than ARG2\n"
         "  ARG1 <= ARG2      ARG1 is less than or equal to ARG2\n"
         "  ARG1 = ARG2       ARG1 is equal to ARG2\n"
         "  ARG1 != ARG2      ARG1 is unequal to ARG2\n"
         "  ARG1 >= ARG2      ARG1 is greater than or equal to ARG2\n"
         "  ARG1 > ARG2       ARG1 is greater than ARG2\n"
         "\n"
         "  ARG1 + ARG2       arithmetic sum of ARG1 and ARG2\n"
         "  ARG1 - ARG2       arithmetic difference of ARG1 and ARG2\n"
         "\n"
         "  ARG1 * ARG2       arithmetic product of ARG1 and ARG2\n"
         "  ARG1 / ARG2       arithmetic quotient of ARG1 divided by ARG2\n"
         "  ARG1 % ARG2       arithmetic remainder of ARG1 divided by ARG2\n"
         "\n"
         "  STRING : REGEXP   anchored pattern match of REGEXP in STRING\n"
         "\n"
         "  match STRING REGEXP        same as STRING : REGEXP\n"
         "  substr STRING POS LENGTH   substring of STRING, POS counted from 1\n"
         "  index STRING CHARS         index in STRING where any CHARS is found, or 0\n"
         "  length STRING              length of STRING\n"
         "  + TOKEN                    interpret TOKEN as a string, even if it is a\n"
         "                               keyword like 'match' or an operator like '/'\n"
         "  ( EXPRESSION )             value of EXPRESSION\n"
         "\n"
         "Beware that many operators need to be escaped or quoted for shells.\n"
         "Comparisons are arithmetic if both ARGs are numbers, else lexicographical.\n"
         "Pattern matches return the string matched between \\( and \\) or null; if\n"
         "\\( and \\) are not used, they return the number of characters matched or 0.\n"
         "\n"
         "Exit status is 0 if EXPRESSION is neither null nor 0, 1 if EXPRESSION is null\n"
         "or 0, 2 if EXPRESSION is syntactically invalid, and 3 if an error occurred.");
}

static void die(int status, const char* format, const char* detail) {
    fprintf(stderr, PROGRAM ": ");
    fprintf(stderr, format, detail);
    fputc('\n', stderr);
    fflush(stdout);
    exit(status);
}

static char* copy_string(const char* s) {
    char* r = _strdup(s);
    if (r == NULL)
        die(STATUS_FAILURE, "memory exhausted%s", "");
    return r;
}

/* ---- integers of any size ------------------------------------------------------------------- */

#define BASE 1000000000u

typedef struct {
    int       negative;
    size_t    count;            // limbs, least significant first, no leading zero limbs
    uint32_t* limbs;
} Big;

static Big big_alloc(size_t count) {
    Big b;
    b.negative = 0;
    b.count = count;
    b.limbs = calloc(count == 0 ? 1 : count, sizeof(uint32_t));
    if (b.limbs == NULL)
        die(STATUS_FAILURE, "memory exhausted%s", "");
    return b;
}

static void big_trim(Big* b) {
    while (b->count > 0 && b->limbs[b->count - 1] == 0)
        b->count--;
    if (b->count == 0)
        b->negative = 0;
}

static int looks_like_integer(const char* s) {
    if (*s == '-')
        s++;
    if (*s == '\0')
        return 0;
    for (; *s != '\0'; s++)
        if (*s < '0' || *s > '9')
            return 0;
    return 1;
}

static Big big_parse(const char* s) {
    Big b;
    size_t length, digits, i;
    int negative = *s == '-';

    if (negative)
        s++;
    while (s[0] == '0' && s[1] != '\0')
        s++;
    length = strlen(s);
    b = big_alloc((length + 8) / 9);
    digits = length;
    for (i = 0; digits > 0; i++) {
        size_t take = digits >= 9 ? 9 : digits;
        uint32_t limb = 0, k;
        for (k = 0; k < take; k++)
            limb = limb * 10 + (uint32_t)(s[digits - take + k] - '0');
        b.limbs[i] = limb;
        digits -= take;
    }
    b.negative = negative;
    big_trim(&b);
    return b;
}

static char* big_format(const Big* b) {
    size_t size = b->count * 9 + 3, i;
    char* out = malloc(size);
    char* p = out;

    if (out == NULL)
        die(STATUS_FAILURE, "memory exhausted%s", "");
    if (b->count == 0) {
        strcpy(out, "0");
        return out;
    }
    if (b->negative)
        *p++ = '-';
    p += sprintf(p, "%u", b->limbs[b->count - 1]);
    for (i = b->count - 1; i-- > 0;)
        p += sprintf(p, "%09u", b->limbs[i]);
    return out;
}

static int big_compare_magnitude(const Big* a, const Big* b) {
    size_t i;
    if (a->count != b->count)
        return a->count < b->count ? -1 : 1;
    for (i = a->count; i-- > 0;)
        if (a->limbs[i] != b->limbs[i])
            return a->limbs[i] < b->limbs[i] ? -1 : 1;
    return 0;
}

static int big_compare(const Big* a, const Big* b) {
    int m;
    if (a->negative != b->negative)
        return a->negative ? -1 : 1;
    m = big_compare_magnitude(a, b);
    return a->negative ? -m : m;
}

static Big add_magnitudes(const Big* a, const Big* b) {
    size_t n = (a->count > b->count ? a->count : b->count) + 1, i;
    Big r = big_alloc(n);
    uint32_t carry = 0;

    for (i = 0; i < n; i++) {
        uint32_t sum = carry;
        if (i < a->count)
            sum += a->limbs[i];
        if (i < b->count)
            sum += b->limbs[i];
        carry = sum >= BASE;
        r.limbs[i] = carry ? sum - BASE : sum;
    }
    big_trim(&r);
    return r;
}

/** a - b, for |a| >= |b|. */
static Big sub_magnitudes(const Big* a, const Big* b) {
    Big r = big_alloc(a->count);
    int64_t borrow = 0;
    size_t i;

    for (i = 0; i < a->count; i++) {
        int64_t d = (int64_t)a->limbs[i] - borrow - (i < b->count ? b->limbs[i] : 0);
        borrow = d < 0;
        r.limbs[i] = (uint32_t)(d < 0 ? d + BASE : d);
    }
    big_trim(&r);
    return r;
}

static Big big_add(const Big* a, const Big* b) {
    Big r;
    if (a->negative == b->negative) {
        r = add_magnitudes(a, b);
        r.negative = a->negative;
    } else if (big_compare_magnitude(a, b) >= 0) {
        r = sub_magnitudes(a, b);
        r.negative = a->negative;
    } else {
        r = sub_magnitudes(b, a);
        r.negative = b->negative;
    }
    big_trim(&r);
    return r;
}

static Big big_multiply(const Big* a, const Big* b) {
    Big r = big_alloc(a->count + b->count + 1);
    size_t i, j;

    for (i = 0; i < a->count; i++) {
        uint64_t carry = 0;
        for (j = 0; j < b->count || carry != 0; j++) {
            uint64_t cur = r.limbs[i + j] + carry + (j < b->count ? (uint64_t)a->limbs[i] * b->limbs[j] : 0);
            r.limbs[i + j] = (uint32_t)(cur % BASE);
            carry = cur / BASE;
        }
    }
    r.negative = a->negative != b->negative;
    big_trim(&r);
    return r;
}

/** magnitude * small (< BASE). */
static Big multiply_small(const Big* a, uint32_t small) {
    Big r = big_alloc(a->count + 1);
    uint64_t carry = 0;
    size_t i;

    for (i = 0; i < a->count; i++) {
        uint64_t cur = (uint64_t)a->limbs[i] * small + carry;
        r.limbs[i] = (uint32_t)(cur % BASE);
        carry = cur / BASE;
    }
    r.limbs[a->count] = (uint32_t)carry;
    big_trim(&r);
    return r;
}

/** Truncating division: quotient and remainder (the remainder has the sign of a), b is not 0. */
static void big_divide(const Big* a, const Big* b, Big* quotient, Big* remainder) {
    Big q = big_alloc(a->count);
    Big rest = big_alloc(0);
    Big divisor = *b;
    size_t i;

    divisor.negative = 0;
    for (i = a->count; i-- > 0;) {
        uint32_t low = 0, high = BASE - 1;
        Big shifted = big_alloc(rest.count + 1);

        // rest = rest * BASE + the next limb
        memcpy(shifted.limbs + 1, rest.limbs, rest.count * sizeof(uint32_t));
        shifted.limbs[0] = a->limbs[i];
        big_trim(&shifted);
        free(rest.limbs);
        rest = shifted;

        // the largest digit d with divisor * d <= rest
        while (low < high) {
            uint32_t mid = low + (high - low + 1) / 2;
            Big product = multiply_small(&divisor, mid);
            int c = big_compare_magnitude(&product, &rest);
            free(product.limbs);
            if (c <= 0)
                low = mid;
            else
                high = mid - 1;
        }
        q.limbs[i] = low;
        if (low != 0) {
            Big product = multiply_small(&divisor, low);
            Big next = sub_magnitudes(&rest, &product);
            free(product.limbs);
            free(rest.limbs);
            rest = next;
        }
    }
    q.negative = a->negative != b->negative;
    rest.negative = a->negative;
    big_trim(&q);
    big_trim(&rest);
    *quotient = q;
    *remainder = rest;
}

/* ---- values --------------------------------------------------------------------------------- */

static int is_null(const char* s) {
    if (*s == '\0')
        return 1;
    if (looks_like_integer(s)) {
        const char* p = s + (*s == '-');
        while (*p == '0')
            p++;
        return *p == '\0';
    }
    return 0;
}

static Big integer_argument(const char* s) {
    if (!looks_like_integer(s))
        die(STATUS_INVALID, "non-integer argument%s", "");
    return big_parse(s);
}

static char* arithmetic(char op, const char* left, const char* right) {
    Big a = integer_argument(left);
    Big b = integer_argument(right);
    Big r;
    char* text;

    if (op == '+') {
        r = big_add(&a, &b);
    } else if (op == '-') {
        Big negated = b;
        negated.negative = b.count != 0 && !b.negative;
        r = big_add(&a, &negated);
    } else if (op == '*') {
        r = big_multiply(&a, &b);
    } else {
        Big q, m;
        if (b.count == 0)
            die(STATUS_INVALID, "division by zero%s", "");
        big_divide(&a, &b, &q, &m);
        if (op == '/') {
            r = q;
            free(m.limbs);
        } else {
            r = m;
            free(q.limbs);
        }
    }
    text = big_format(&r);
    free(a.limbs);
    free(b.limbs);
    free(r.limbs);
    return text;
}

static char* compare_values(const char* op, const char* left, const char* right) {
    int c;

    if (looks_like_integer(left) && looks_like_integer(right)) {
        Big a = big_parse(left), b = big_parse(right);
        c = big_compare(&a, &b);
        free(a.limbs);
        free(b.limbs);
    } else {
        c = strcmp(left, right);
    }
    if (strcmp(op, "<") == 0)
        c = c < 0;
    else if (strcmp(op, "<=") == 0)
        c = c <= 0;
    else if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0)
        c = c == 0;
    else if (strcmp(op, "!=") == 0)
        c = c != 0;
    else if (strcmp(op, ">=") == 0)
        c = c >= 0;
    else
        c = c > 0;
    return copy_string(c ? "1" : "0");
}

/** STRING : REGEXP. */
static char* match(const char* string, const char* pattern) {
    Regex* re;
    RegexInput input;
    char error[200];
    const char* patterns[1];
    size_t start, end, groups;
    char* result;

    patterns[0] = pattern;
    re = regex_compile(patterns, 1, REGEX_GROUPS, error, sizeof error);
    if (re == NULL)
        die(STATUS_INVALID, "%s", error);
    groups = regex_group_count(re);

    memset(&input, 0, sizeof input);
    if (regex_input_set(&input, (const unsigned char*)string, strlen(string)) != 0)
        die(STATUS_FAILURE, "memory exhausted%s", "");

    if (!regex_search(re, &input, 0, &start, &end) || start != 0) {
        result = copy_string(groups > 0 ? "" : "0");
    } else if (groups > 0) {
        size_t* spans = malloc((groups + 1) * 2 * sizeof *spans);
        if (spans == NULL)
            die(STATUS_FAILURE, "memory exhausted%s", "");
        regex_groups(re, &input, start, end, spans);
        if (spans[2] == (size_t)-1 || spans[3] == (size_t)-1) {
            result = copy_string("");
        } else {
            size_t from = input.offset[spans[2]], to = input.offset[spans[3]];
            result = malloc(to - from + 1);
            if (result == NULL)
                die(STATUS_FAILURE, "memory exhausted%s", "");
            memcpy(result, string + from, to - from);
            result[to - from] = '\0';
        }
        free(spans);
    } else {
        char number[32];
        sprintf(number, "%zu", end);
        result = copy_string(number);
    }
    regex_input_free(&input);
    regex_free(re);
    return result;
}

/** The number of characters, and the byte offsets of them (offsets has room for length + 1). */
static size_t characters(const char* s, size_t* offsets) {
    size_t i = 0, n = 0, length = strlen(s);
    while (i < length) {
        size_t used;
        utf8_decode((const unsigned char*)s + i, length - i, &used);
        if (offsets != NULL)
            offsets[n] = i;
        n++;
        i += used;
    }
    if (offsets != NULL)
        offsets[n] = length;
    return n;
}

static char* substring(const char* s, const char* posText, const char* lengthText) {
    Big pos, length;
    size_t count = characters(s, NULL);
    size_t* offsets = malloc((count + 1) * sizeof *offsets);
    char* result;
    long long from, take;
    size_t byteFrom, byteTo;

    if (!looks_like_integer(posText) || !looks_like_integer(lengthText))
        return copy_string("");
    pos = big_parse(posText);
    length = big_parse(lengthText);
    if (offsets == NULL)
        die(STATUS_FAILURE, "memory exhausted%s", "");
    characters(s, offsets);

    // numbers beyond what a string can have act as the extreme they are
    from = pos.negative || pos.count == 0 ? 0 : (pos.count > 1 ? (long long)count + 1 : (long long)pos.limbs[0]);
    take = length.negative || length.count == 0 ? 0 : (length.count > 1 ? (long long)count : (long long)length.limbs[0]);
    free(pos.limbs);
    free(length.limbs);

    if (from < 1 || take < 1 || from > (long long)count) {
        free(offsets);
        return copy_string("");
    }
    if (take > (long long)count - from + 1)
        take = (long long)count - from + 1;
    byteFrom = offsets[from - 1];
    byteTo = offsets[from - 1 + take];
    result = malloc(byteTo - byteFrom + 1);
    if (result == NULL)
        die(STATUS_FAILURE, "memory exhausted%s", "");
    memcpy(result, s + byteFrom, byteTo - byteFrom);
    result[byteTo - byteFrom] = '\0';
    free(offsets);
    return result;
}

static char* index_of(const char* s, const char* chars) {
    size_t i = 0, n = 0, length = strlen(s), setLength = strlen(chars);
    char number[32];

    while (i < length) {
        size_t used, k = 0;
        uint32_t c = utf8_decode((const unsigned char*)s + i, length - i, &used);
        n++;
        while (k < setLength) {
            size_t u;
            uint32_t d = utf8_decode((const unsigned char*)chars + k, setLength - k, &u);
            if (d == c) {
                sprintf(number, "%zu", n);
                return copy_string(number);
            }
            k += u;
        }
        i += used;
    }
    return copy_string("0");
}

/* ---- the parser: one function for every level of precedence --------------------------------- */

static char** args;
static int argCount, position;

static const char* peek_arg(void) {
    return position < argCount ? args[position] : NULL;
}

static int next_is(const char* word) {
    if (position < argCount && strcmp(args[position], word) == 0) {
        position++;
        return 1;
    }
    return 0;
}

static const char* take_arg(void) {
    if (position >= argCount)
        die(STATUS_INVALID, "syntax error: missing argument%s", "");
    return args[position++];
}

static char* eval_or(int evaluate);

/** A parenthesis, a keyword with its arguments, or a plain string. */
static char* eval_primary(int evaluate) {
    const char* token = take_arg();

    if (strcmp(token, "(") == 0) {
        char* value = eval_or(evaluate);
        if (!next_is(")"))
            die(STATUS_INVALID, "syntax error: expecting ')'%s", "");
        return value;
    }
    if (strcmp(token, "+") == 0)
        return copy_string(take_arg());

    if (strcmp(token, "length") == 0) {
        char* s = eval_primary(evaluate);
        char number[32];
        sprintf(number, "%zu", characters(s, NULL));
        free(s);
        return copy_string(number);
    }
    if (strcmp(token, "match") == 0 || strcmp(token, "index") == 0) {
        char* s = eval_primary(evaluate);
        char* t = eval_primary(evaluate);
        char* result = evaluate ? (token[0] == 'm' ? match(s, t) : index_of(s, t)) : copy_string("");
        free(s);
        free(t);
        return result;
    }
    if (strcmp(token, "substr") == 0) {
        char* s = eval_primary(evaluate);
        char* pos = eval_primary(evaluate);
        char* length = eval_primary(evaluate);
        char* result = evaluate ? substring(s, pos, length) : copy_string("");
        free(s);
        free(pos);
        free(length);
        return result;
    }
    if (strcmp(token, ")") == 0)
        die(STATUS_INVALID, "syntax error: unexpected ')'%s", "");
    return copy_string(token);
}

static char* eval_match(int evaluate) {
    char* left = eval_primary(evaluate);
    while (next_is(":")) {
        char* right = eval_primary(evaluate);
        char* result = evaluate ? match(left, right) : copy_string("");
        free(left);
        free(right);
        left = result;
    }
    return left;
}

static char* eval_term(int evaluate) {
    char* left = eval_match(evaluate);
    for (;;) {
        const char* p = peek_arg();
        char op;
        char* right;
        char* result;

        if (p == NULL || p[1] != '\0' || (p[0] != '*' && p[0] != '/' && p[0] != '%'))
            return left;
        op = p[0];
        position++;
        right = eval_match(evaluate);
        result = evaluate ? arithmetic(op, left, right) : copy_string("");
        free(left);
        free(right);
        left = result;
    }
}

static char* eval_sum(int evaluate) {
    char* left = eval_term(evaluate);
    for (;;) {
        const char* p = peek_arg();
        char op;
        char* right;
        char* result;

        if (p == NULL || p[1] != '\0' || (p[0] != '+' && p[0] != '-'))
            return left;
        op = p[0];
        position++;
        right = eval_term(evaluate);
        result = evaluate ? arithmetic(op, left, right) : copy_string("");
        free(left);
        free(right);
        left = result;
    }
}

static int is_comparison(const char* p) {
    static const char* const ops[] = { "<", "<=", "=", "==", "!=", ">=", ">" };
    size_t i;
    if (p == NULL)
        return 0;
    for (i = 0; i < sizeof ops / sizeof ops[0]; i++)
        if (strcmp(p, ops[i]) == 0)
            return 1;
    return 0;
}

static char* eval_comparison(int evaluate) {
    char* left = eval_sum(evaluate);
    while (is_comparison(peek_arg())) {
        const char* op = take_arg();
        char* right = eval_sum(evaluate);
        char* result = evaluate ? compare_values(op, left, right) : copy_string("");
        free(left);
        free(right);
        left = result;
    }
    return left;
}

static char* eval_and(int evaluate) {
    char* left = eval_comparison(evaluate);
    while (next_is("&")) {
        char* right = eval_comparison(evaluate && !is_null(left));
        if (is_null(left) || is_null(right)) {
            free(left);
            left = copy_string("0");
        }
        free(right);
    }
    return left;
}

static char* eval_or(int evaluate) {
    char* left = eval_and(evaluate);
    while (next_is("|")) {
        char* right = eval_and(evaluate && is_null(left));
        if (is_null(left)) {
            free(left);
            if (is_null(right)) {
                free(right);
                right = copy_string("0");
            }
            left = right;
        } else {
            free(right);
        }
    }
    return left;
}

int main(int argc, char* argv[]) {
    char* value;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf(PROGRAM " (Winshell) " VERSION "\n");
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, PROGRAM ": missing operand\nTry '" PROGRAM " --help' for more information.\n");
        return STATUS_INVALID;
    }

    _setmode(_fileno(stdout), _O_BINARY);
    args = argv + 1;
    argCount = argc - 1;
    if (strcmp(args[0], "--") == 0 && argCount > 1) {
        args++;
        argCount--;
    }
    position = 0;

    value = eval_or(1);
    if (position < argCount)
        die(STATUS_INVALID, "syntax error: unexpected argument '%s'", args[position]);

    fputs(value, stdout);
    fputc('\n', stdout);
    return is_null(value) ? 1 : 0;
}
