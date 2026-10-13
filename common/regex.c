#include "regex.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include "utf8.h"
#include "vec.h"

#define INVALID_BASE UTF8_INVALID_BASE  /* chars at and above this are bytes that were not UTF-8 */
#define DUP_MAX      32767
#define PROGRAM_MAX  400000
#define BACKTRACK_STEPS 20000000L

/* ---- characters ------------------------------------------------------------------ */

#define decode_char utf8_decode

int regex_input_set(RegexInput* input, const unsigned char* bytes, size_t length) {
    size_t i = 0, n = 0;
    size_t oldCapacity = input->capacity;

    if (VEC_RESERVE(input->chars, input->capacity, length + 1) != 0)
        return -1;
    if (input->capacity != oldCapacity || input->offset == NULL) {
        // the offsets are as many as the characters, and one more: they follow the capacity of the chars
        size_t* grown = realloc(input->offset, input->capacity * sizeof *grown);
        if (grown == NULL)
            return -1;
        input->offset = grown;
    }

    while (i < length) {
        size_t used;
        input->chars[n] = decode_char(bytes + i, length - i, &used);
        input->offset[n] = i;
        n++;
        i += used;
    }
    input->offset[n] = length;
    input->length = n;
    return 0;
}

void regex_input_free(RegexInput* input) {
    free(input->chars);
    free(input->offset);
    memset(input, 0, sizeof *input);
}

#define fold_char  utf8_fold
#define upper_char utf8_upper

/* character classes */
#define CL_ALPHA  0x0001
#define CL_DIGIT  0x0002
#define CL_ALNUM  0x0004
#define CL_UPPER  0x0008
#define CL_LOWER  0x0010
#define CL_SPACE  0x0020
#define CL_BLANK  0x0040
#define CL_PUNCT  0x0080
#define CL_PRINT  0x0100
#define CL_GRAPH  0x0200
#define CL_CNTRL  0x0400
#define CL_XDIGIT 0x0800
#define CL_WORD   0x1000        /* [_[:alnum:]], for \w */

static int in_class(uint32_t c, unsigned classes) {
    if (c >= INVALID_BASE)
        return 0;

    if (c < 0x80) {
        int upper = c >= 'A' && c <= 'Z', lower = c >= 'a' && c <= 'z', digit = c >= '0' && c <= '9';
        if ((classes & CL_ALPHA) && (upper || lower)) return 1;
        if ((classes & CL_DIGIT) && digit) return 1;
        if ((classes & CL_ALNUM) && (upper || lower || digit)) return 1;
        if ((classes & CL_UPPER) && upper) return 1;
        if ((classes & CL_LOWER) && lower) return 1;
        if ((classes & CL_SPACE) && (c == ' ' || (c >= 9 && c <= 13))) return 1;
        if ((classes & CL_BLANK) && (c == ' ' || c == '\t')) return 1;
        if ((classes & CL_PUNCT) && c > 32 && c < 127 && !(upper || lower || digit)) return 1;
        if ((classes & CL_PRINT) && c >= 32 && c < 127) return 1;
        if ((classes & CL_GRAPH) && c > 32 && c < 127) return 1;
        if ((classes & CL_CNTRL) && (c < 32 || c == 127)) return 1;
        if ((classes & CL_XDIGIT) && (digit || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 1;
        if ((classes & CL_WORD) && (upper || lower || digit || c == '_')) return 1;
        return 0;
    }

    if (c >= 0x10000)
        return (classes & (CL_PRINT | CL_GRAPH)) != 0;
    {
        wint_t w = (wint_t)c;
        if ((classes & CL_ALPHA) && iswalpha(w)) return 1;
        if ((classes & CL_ALNUM) && iswalnum(w)) return 1;
        if ((classes & CL_WORD) && iswalnum(w)) return 1;
        if ((classes & CL_UPPER) && (iswupper(w) || fold_char(c) != c)) return 1;
        if ((classes & CL_LOWER) && (iswlower(w) || upper_char(c) != c)) return 1;
        if ((classes & CL_SPACE) && iswspace(w)) return 1;
        if ((classes & CL_PUNCT) && iswpunct(w)) return 1;
        if ((classes & CL_PRINT) && iswprint(w)) return 1;
        if ((classes & CL_GRAPH) && iswgraph(w)) return 1;
        if ((classes & CL_CNTRL) && iswcntrl(w)) return 1;
    }
    return 0;
}

static int is_word_char(uint32_t c) {
    return in_class(c, CL_WORD);
}

/* ---- the tree ---------------------------------------------------------------------- */

typedef struct {
    uint32_t (*ranges)[2];
    size_t   rangeCount, rangeCapacity;
    unsigned classes;
    int      negate;
} Set;

typedef enum {
    N_NONE, N_EMPTY, N_CHAR, N_ANY, N_SET, N_CAT, N_ALT, N_REPEAT, N_GROUP, N_BACKREF, N_ASSERT
} NodeKind;

typedef enum {
    A_BOL, A_EOL, A_WORDB, A_NWORDB, A_WSTART, A_WEND, A_NOT_WORD_BEFORE, A_NOT_WORD_AFTER
} AssertKind;

typedef struct Node {
    NodeKind     kind;
    uint32_t     ch;            /* N_CHAR */
    int          set;           /* N_SET: index into the sets */
    int          min, max;      /* N_REPEAT: max -1 is no limit */
    int          group;         /* N_GROUP, N_BACKREF */
    AssertKind   assertKind;
    struct Node* a;
    struct Node* b;
} Node;

typedef enum { I_CHAR, I_ANY, I_SET, I_SPLIT, I_JMP, I_SAVE, I_BACK, I_ASSERT, I_FAIL, I_MATCH } Op;

typedef struct {
    Op       op;
    int      x, y;
    uint32_t c;
} Inst;

struct Regex {
    Inst*  program;
    size_t length;
    Set*   sets;
    size_t setCount;
    int    groupCount;
    int    hasBackref;
    int    icase;
};

/* ---- the parser ----------------------------------------------------------------- */

typedef struct {
    const uint32_t* pat;
    size_t          length, i;
    int             extended, icase;
    int             groupBase, groupsHere;      /* group numbers of this pattern start after groupBase */
    int             closedHere[10];             /* for \1..\9: whether that group of this pattern is closed */
    int             hasBackref;
    Node**          nodes;                      /* everything allocated, to be freed at the end */
    size_t          nodeCount, nodeCapacity;
    Set*            sets;
    size_t          setCount, setCapacity;
    jmp_buf         failure;
    const char*     message;
    int             nomem;
    uint32_t*       chars;                      /* the pattern being parsed, decoded */
    size_t          charCapacity;
} Parser;

static void fail(Parser* p, const char* message) {
    p->message = message;
    longjmp(p->failure, 1);
}

static Node* new_node(Parser* p, NodeKind kind) {
    Node* n = calloc(1, sizeof *n);
    if (n == NULL || VEC_RESERVE(p->nodes, p->nodeCapacity, p->nodeCount + 1) != 0) {
        free(n);
        p->nomem = 1;
        fail(p, "Memory exhausted");
    }
    n->kind = kind;
    p->nodes[p->nodeCount++] = n;
    return n;
}

static Node* make_cat(Parser* p, Node* a, Node* b) {
    Node* n;
    if (a->kind == N_EMPTY)
        return b;
    if (b->kind == N_EMPTY)
        return a;
    n = new_node(p, N_CAT);
    n->a = a;
    n->b = b;
    return n;
}

static Node* make_char(Parser* p, uint32_t c) {
    Node* n = new_node(p, N_CHAR);
    n->ch = p->icase ? fold_char(c) : c;
    return n;
}

static Node* make_assert(Parser* p, AssertKind kind) {
    Node* n = new_node(p, N_ASSERT);
    n->assertKind = kind;
    return n;
}

static int new_set(Parser* p) {
    if (VEC_RESERVE(p->sets, p->setCapacity, p->setCount + 1) != 0) {
        p->nomem = 1;
        fail(p, "Memory exhausted");
    }
    memset(&p->sets[p->setCount], 0, sizeof p->sets[0]);
    return (int)p->setCount++;
}

static void set_add_range(Parser* p, int set, uint32_t lo, uint32_t hi) {
    Set* s = &p->sets[set];
    if (VEC_RESERVE(s->ranges, s->rangeCapacity, s->rangeCount + 1) != 0) {
        p->nomem = 1;
        fail(p, "Memory exhausted");
    }
    s->ranges[s->rangeCount][0] = lo;
    s->ranges[s->rangeCount][1] = hi;
    s->rangeCount++;
}

static int more(const Parser* p) {
    return p->i < p->length;
}

static uint32_t at(const Parser* p, size_t k) {
    return p->i + k < p->length ? p->pat[p->i + k] : 0xFFFFFFFFu;
}

/** At "|" (extended) or "\|" (basic). */
static int at_alternation(const Parser* p) {
    if (p->extended)
        return at(p, 0) == '|';
    return at(p, 0) == '\\' && at(p, 1) == '|';
}

/** At ")" (extended, when a group is open: elsewhere it is a character) or "\)" (basic). */
static int at_close(const Parser* p, int depth) {
    if (p->extended)
        return depth > 0 && at(p, 0) == ')';
    return at(p, 0) == '\\' && at(p, 1) == ')';
}

static unsigned class_by_name(const uint32_t* name, size_t length) {
    static const struct { const char* name; unsigned bit; } names[] = {
        { "alpha", CL_ALPHA }, { "digit", CL_DIGIT }, { "alnum", CL_ALNUM }, { "upper", CL_UPPER },
        { "lower", CL_LOWER }, { "space", CL_SPACE }, { "blank", CL_BLANK }, { "punct", CL_PUNCT },
        { "print", CL_PRINT }, { "graph", CL_GRAPH }, { "cntrl", CL_CNTRL }, { "xdigit", CL_XDIGIT },
    };
    size_t k, j;

    for (k = 0; k < sizeof names / sizeof names[0]; k++) {
        const char* s = names[k].name;
        if (strlen(s) != length)
            continue;
        for (j = 0; j < length && name[j] == (uint32_t)(unsigned char)s[j]; j++)
            ;
        if (j == length)
            return names[k].bit;
    }
    return 0;
}

/** After "[": the set up to and including "]". */
static Node* parse_bracket(Parser* p) {
    int set = new_set(p);
    Node* n;
    int first = 1;
    size_t start = p->i;

    if (at(p, 0) == '^') {
        p->sets[set].negate = 1;
        p->i++;
    }

    for (;;) {
        uint32_t lo, hi;

        if (!more(p))
            fail(p, "Unmatched [, [^, [:, [., or [=");
        lo = p->pat[p->i];

        if (lo == ']' && !first) {
            p->i++;
            break;
        }
        first = 0;

        if (lo == '[' && (at(p, 1) == ':' || at(p, 1) == '.' || at(p, 1) == '=')) {
            uint32_t kind = at(p, 1);
            size_t k = p->i + 2, end = k;

            while (end + 1 < p->length && !(p->pat[end] == kind && p->pat[end + 1] == ']'))
                end++;
            if (end + 1 >= p->length)
                fail(p, "Unmatched [, [^, [:, [., or [=");

            if (kind == ':') {
                unsigned bit = class_by_name(p->pat + k, end - k);
                if (bit == 0)
                    fail(p, "Invalid character class name");
                p->sets[set].classes |= bit;
                p->i = end + 2;
                continue;
            }
            // [.x.] and [=x=]: one character, which stands for itself
            if (end - k != 1)
                fail(p, "Invalid collation character");
            lo = p->pat[k];
            p->i = end + 2;
        } else {
            p->i++;
        }

        // a range: lo-hi, where a "-" in front of "]" is a character
        hi = lo;
        if (at(p, 0) == '-' && at(p, 1) != ']' && at(p, 1) != 0xFFFFFFFFu) {
            p->i++;
            hi = p->pat[p->i];
            if (hi == '[' && at(p, 1) == '.') {
                size_t k = p->i + 2, end = k;
                while (end + 1 < p->length && !(p->pat[end] == '.' && p->pat[end + 1] == ']'))
                    end++;
                if (end + 1 >= p->length || end - k != 1)
                    fail(p, "Invalid collation character");
                hi = p->pat[k];
                p->i = end + 2;
            } else {
                p->i++;
            }
            if (hi < lo)
                fail(p, "Invalid range end");
        }
        set_add_range(p, set, lo, hi);
    }

    // "[:space:]" outside of a bracket expression is a mistake GNU grep points out
    if (p->i - start >= 3 && p->pat[start] == ':' && p->pat[p->i - 2] == ':')
        fail(p, "character class syntax is [[:space:]], not [:space:]");

    n = new_node(p, N_SET);
    n->set = set;
    return n;
}

static Node* make_class_set(Parser* p, unsigned classes, int negate) {
    int set = new_set(p);
    Node* n;
    p->sets[set].classes = classes;
    p->sets[set].negate = negate;
    n = new_node(p, N_SET);
    n->set = set;
    return n;
}

static Node* parse_alternation(Parser* p, int depth);

/** A group; i is after the opening parenthesis. */
static Node* parse_group(Parser* p, int depth) {
    Node* n = new_node(p, N_GROUP);
    int number = ++p->groupsHere;

    n->group = p->groupBase + number;
    n->a = parse_alternation(p, depth + 1);
    if (!at_close(p, depth + 1))
        fail(p, "Unmatched ( or \\(");
    p->i += p->extended ? 1 : 2;
    if (number < 10)
        p->closedHere[number] = 1;
    return n;
}

/** A backslash sequence outside of brackets; i is after the backslash. */
static Node* parse_escape(Parser* p) {
    uint32_t c;

    if (!more(p))
        fail(p, "Trailing backslash");
    c = p->pat[p->i++];

    switch (c) {
        case 'w': return make_class_set(p, CL_WORD, 0);
        case 'W': return make_class_set(p, CL_WORD, 1);
        case 's': return make_class_set(p, CL_SPACE, 0);
        case 'S': return make_class_set(p, CL_SPACE, 1);
        case 'b': return make_assert(p, A_WORDB);
        case 'B': return make_assert(p, A_NWORDB);
        case '<': return make_assert(p, A_WSTART);
        case '>': return make_assert(p, A_WEND);
        case '`': return make_assert(p, A_BOL);
        case '\'': return make_assert(p, A_EOL);
        default: break;
    }

    if (c >= '1' && c <= '9') {
        Node* n;
        int number = (int)(c - '0');
        if (number > p->groupsHere || !p->closedHere[number])
            fail(p, "Invalid back reference");
        n = new_node(p, N_BACKREF);
        n->group = p->groupBase + number;
        p->hasBackref = 1;
        return n;
    }
    return make_char(p, c);
}

/** What follows a "{" or "\{": m, m,, m,n or ,n and the closing brace. 0 when it is not an interval. */
static int parse_interval(Parser* p, size_t at_index, int* min, int* max, size_t* after) {
    size_t i = at_index;
    long lo = 0, hi = -1;
    int haveLo = 0;

    while (i < p->length && p->pat[i] >= '0' && p->pat[i] <= '9') {
        lo = lo * 10 + (long)(p->pat[i] - '0');
        if (lo > DUP_MAX + 1)
            lo = DUP_MAX + 1;
        haveLo = 1;
        i++;
    }
    if (i < p->length && p->pat[i] == ',') {
        i++;
        if (i < p->length && p->pat[i] >= '0' && p->pat[i] <= '9') {
            hi = 0;
            while (i < p->length && p->pat[i] >= '0' && p->pat[i] <= '9') {
                hi = hi * 10 + (long)(p->pat[i] - '0');
                if (hi > DUP_MAX + 1)
                    hi = DUP_MAX + 1;
                i++;
            }
        }
        if (!haveLo)
            lo = 0;
    } else {
        if (!haveLo)
            return 0;
        hi = lo;
    }

    if (p->extended) {
        if (i >= p->length || p->pat[i] != '}')
            return 0;
        i++;
    } else {
        if (i + 1 >= p->length || p->pat[i] != '\\' || p->pat[i + 1] != '}')
            return 0;
        i += 2;
    }
    if (lo > DUP_MAX || hi > DUP_MAX)
        fail(p, "Regular expression too big");
    if (hi != -1 && lo > hi)
        fail(p, "Invalid content of \\{\\}");
    *min = (int)lo;
    *max = (int)hi;
    *after = i;
    return 1;
}

static Node* make_repeat(Parser* p, Node* a, int min, int max) {
    Node* n = new_node(p, N_REPEAT);
    n->a = a;
    n->min = min;
    n->max = max;
    return n;
}

/** The repetition operators after an atom. */
static Node* parse_postfix(Parser* p, Node* atom) {
    for (;;) {
        uint32_t c = at(p, 0);
        int min, max;
        size_t after;

        if (c == '*') {
            p->i++;
            atom = make_repeat(p, atom, 0, -1);
        } else if (p->extended && c == '+') {
            p->i++;
            atom = make_repeat(p, atom, 1, -1);
        } else if (p->extended && c == '?') {
            p->i++;
            atom = make_repeat(p, atom, 0, 1);
        } else if (p->extended && c == '{') {
            if (!parse_interval(p, p->i + 1, &min, &max, &after))
                return atom;            // a "{" that starts no interval is a character
            p->i = after;
            atom = make_repeat(p, atom, min, max);
        } else if (!p->extended && c == '\\' && at(p, 1) == '+') {
            p->i += 2;
            atom = make_repeat(p, atom, 1, -1);
        } else if (!p->extended && c == '\\' && at(p, 1) == '?') {
            p->i += 2;
            atom = make_repeat(p, atom, 0, 1);
        } else if (!p->extended && c == '\\' && at(p, 1) == '{') {
            if (!parse_interval(p, p->i + 2, &min, &max, &after)) {
                // distinguish a missing "\}" from a bad count
                size_t k = p->i + 2;
                while (k + 1 < p->length && !(p->pat[k] == '\\' && p->pat[k + 1] == '}'))
                    k++;
                fail(p, k + 1 < p->length ? "Invalid content of \\{\\}" : "Unmatched \\{");
            }
            p->i = after;
            atom = make_repeat(p, atom, min, max);
        } else {
            return atom;
        }
    }
}

/** Whether, in a basic expression, a "$" at the current place is an anchor. */
static int dollar_is_anchor(const Parser* p) {
    size_t next = p->i + 1;
    if (next >= p->length)
        return 1;
    return p->pat[next] == '\\' && next + 1 < p->length && (p->pat[next + 1] == ')' || p->pat[next + 1] == '|');
}

/** One atom. atStart: nothing in this expression yet (or only an anchor), where a basic "*" is a character. */
static Node* parse_atom(Parser* p, int depth, int atStart) {
    uint32_t c = p->pat[p->i];

    if (p->extended) {
        switch (c) {
            case '(':
                p->i++;
                return parse_group(p, depth);
            case '.':
                p->i++;
                return new_node(p, N_ANY);
            case '[':
                p->i++;
                return parse_bracket(p);
            case '^':
                p->i++;
                return make_assert(p, A_BOL);
            case '$':
                p->i++;
                return make_assert(p, A_EOL);
            case '\\':
                p->i++;
                return parse_escape(p);
            default:
                p->i++;
                return make_char(p, c);         // also a "*", "+", "?" or "{" with nothing before it
        }
    }

    switch (c) {
        case '.':
            p->i++;
            return new_node(p, N_ANY);
        case '[':
            p->i++;
            return parse_bracket(p);
        case '^':
            p->i++;
            return atStart ? make_assert(p, A_BOL) : make_char(p, c);
        case '$':
            if (dollar_is_anchor(p)) {
                p->i++;
                return make_assert(p, A_EOL);
            }
            p->i++;
            return make_char(p, c);
        case '\\':
            if (at(p, 1) == '(') {
                p->i += 2;
                return parse_group(p, depth);
            }
            if (at(p, 1) == '{' && atStart)
                fail(p, "Invalid preceding regular expression");
            if (atStart && (at(p, 1) == '+' || at(p, 1) == '?')) {
                p->i += 2;
                return make_char(p, p->pat[p->i - 1]);
            }
            p->i++;
            return parse_escape(p);
        default:
            p->i++;
            return make_char(p, c);             // including a "*" with nothing before it
    }
}

static Node* parse_concatenation(Parser* p, int depth) {
    Node* result = new_node(p, N_EMPTY);
    int atStart = 1;

    while (more(p) && !at_alternation(p) && !at_close(p, depth)) {
        Node* atom;
        int anchorOnly, emptyOperand = 0;

        // extended: a repetition operator with nothing to repeat repeats nothing, as GNU does ("*a" is "a")
        if (p->extended && atStart && (p->pat[p->i] == '*' || p->pat[p->i] == '+' || p->pat[p->i] == '?' ||
                                       p->pat[p->i] == '{')) {
            int min, max;
            size_t after;
            if (p->pat[p->i] != '{' || parse_interval(p, p->i + 1, &min, &max, &after)) {
                atom = new_node(p, N_EMPTY);
                anchorOnly = 0;
                emptyOperand = 1;
            } else {
                atom = parse_atom(p, depth, atStart);       // a "{" that is no interval
                anchorOnly = 0;
            }
        } else {
            atom = parse_atom(p, depth, atStart);
            anchorOnly = atom->kind == N_ASSERT && atom->assertKind == A_BOL && !p->extended;
        }

        // an anchor takes no repetition in basic syntax: a "*" after "^" is a character
        if (!anchorOnly)
            atom = parse_postfix(p, atom);
        // GNU refuses a repetition operator that has nothing in front of it and the end of the group behind
        if (emptyOperand && depth > 0 && at(p, 0) == ')')
            fail(p, "Invalid preceding regular expression");

        result = make_cat(p, result, atom);
        atStart = anchorOnly && atStart;
    }

    // unmatched close in extended syntax at the top level
    return result;
}

static Node* parse_alternation(Parser* p, int depth) {
    Node* left = parse_concatenation(p, depth);

    while (at_alternation(p)) {
        Node* alt;
        p->i += p->extended ? 1 : 2;
        alt = new_node(p, N_ALT);
        alt->a = left;
        alt->b = parse_concatenation(p, depth);
        left = alt;
    }
    return left;
}

/** The whole of one pattern. */
static Node* parse_pattern(Parser* p) {
    Node* tree = parse_alternation(p, 0);

    if (more(p)) {
        // only an unmatched close can stop the parse here
        fail(p, "Unmatched ) or \\)");
    }
    return tree;
}

/* ---- the program --------------------------------------------------------------- */

typedef struct {
    Inst*  code;
    size_t length, capacity;
    int    withSaves;
    int    tooBig;
} Emitter;

static int emit(Emitter* e, Op op, int x, int y, uint32_t c) {
    if (e->tooBig)
        return -1;
    if (e->length >= PROGRAM_MAX || VEC_RESERVE(e->code, e->capacity, e->length + 1) != 0) {
        e->tooBig = 1;
        return -1;
    }
    e->code[e->length].op = op;
    e->code[e->length].x = x;
    e->code[e->length].y = y;
    e->code[e->length].c = c;
    return (int)e->length++;
}

static void emit_node(Emitter* e, const Node* n) {
    int split, jump, k;

    if (e->tooBig)
        return;

    switch (n->kind) {
        case N_NONE:
            emit(e, I_FAIL, 0, 0, 0);
            break;
        case N_EMPTY:
            break;
        case N_CHAR:
            emit(e, I_CHAR, 0, 0, n->ch);
            break;
        case N_ANY:
            emit(e, I_ANY, 0, 0, 0);
            break;
        case N_SET:
            emit(e, I_SET, n->set, 0, 0);
            break;
        case N_CAT:
            emit_node(e, n->a);
            emit_node(e, n->b);
            break;
        case N_ALT:
            split = emit(e, I_SPLIT, 0, 0, 0);
            if (split < 0)
                return;
            e->code[split].x = (int)e->length;
            emit_node(e, n->a);
            jump = emit(e, I_JMP, 0, 0, 0);
            if (jump < 0)
                return;
            e->code[split].y = (int)e->length;
            emit_node(e, n->b);
            e->code[jump].x = (int)e->length;
            break;
        case N_GROUP:
            if (e->withSaves)
                emit(e, I_SAVE, n->group * 2, 0, 0);
            emit_node(e, n->a);
            if (e->withSaves)
                emit(e, I_SAVE, n->group * 2 + 1, 0, 0);
            break;
        case N_BACKREF:
            emit(e, I_BACK, n->group, 0, 0);
            break;
        case N_ASSERT:
            emit(e, I_ASSERT, (int)n->assertKind, 0, 0);
            break;
        case N_REPEAT: {
            int min = n->min, max = n->max;

            for (k = 0; k < min; k++)
                emit_node(e, n->a);

            if (max < 0) {
                // a*: L1: split L2, L3; L2: a; jmp L1; L3:
                int l1 = (int)e->length;
                split = emit(e, I_SPLIT, 0, 0, 0);
                if (split < 0)
                    return;
                e->code[split].x = (int)e->length;
                emit_node(e, n->a);
                emit(e, I_JMP, l1, 0, 0);
                e->code[split].y = (int)e->length;
            } else if (max > min) {
                // up to max-min more, each one only if the one before it was taken: (a(a(a)?)?)?
                // every split leaves to the end of all of them when it is not taken
                int count = max - min;
                int* splits = malloc((size_t)count * sizeof *splits);

                if (splits == NULL) {
                    e->tooBig = 1;
                    return;
                }
                for (k = 0; k < count; k++) {
                    splits[k] = emit(e, I_SPLIT, 0, 0, 0);
                    if (splits[k] < 0) {
                        free(splits);
                        return;
                    }
                    e->code[splits[k]].x = (int)e->length;
                    emit_node(e, n->a);
                }
                for (k = 0; k < count; k++)
                    e->code[splits[k]].y = (int)e->length;
                free(splits);
            }
            break;
        }
    }
}

/* ---- matching ------------------------------------------------------------------- */

static int assertion_holds(AssertKind kind, const uint32_t* t, size_t n, size_t pos) {
    int before = pos > 0 && is_word_char(t[pos - 1]);
    int after = pos < n && is_word_char(t[pos]);

    switch (kind) {
        case A_BOL:              return pos == 0;
        case A_EOL:              return pos == n;
        case A_WORDB:            return before != after;
        case A_NWORDB:           return before == after;
        case A_WSTART:           return !before && after;
        case A_WEND:             return before && !after;
        case A_NOT_WORD_BEFORE:  return !before;
        case A_NOT_WORD_AFTER:   return !after;
    }
    return 0;
}

static int set_matches(const Regex* re, const Set* set, uint32_t c) {
    size_t k;
    int found = 0;

    if (c >= INVALID_BASE)
        return 0;

    for (k = 0; k < set->rangeCount && !found; k++)
        found = c >= set->ranges[k][0] && c <= set->ranges[k][1];
    if (!found && set->classes != 0)
        found = in_class(c, set->classes);
    if (!found && re->icase) {
        uint32_t lower = fold_char(c), upper = upper_char(c);
        for (k = 0; k < set->rangeCount && !found; k++)
            found = (lower >= set->ranges[k][0] && lower <= set->ranges[k][1]) ||
                    (upper >= set->ranges[k][0] && upper <= set->ranges[k][1]);
        if (!found && set->classes != 0)
            found = in_class(lower, set->classes) || in_class(upper, set->classes);
    }
    return found != set->negate;
}

/** Whether the consuming instruction accepts the character. */
static int consume_ok(const Regex* re, const Inst* in, uint32_t c) {
    switch (in->op) {
        case I_CHAR: return (re->icase ? fold_char(c) : c) == in->c;
        case I_ANY:  return c < INVALID_BASE;
        case I_SET:  return set_matches(re, &re->sets[in->x], c);
        default:     return 0;
    }
}

typedef struct {
    int    pc;
    size_t start;
} Thread;

typedef struct {
    Thread* threads;
    size_t  count;
} ThreadList;

typedef struct {
    const Regex*    re;
    const uint32_t* t;
    size_t          n;
    int             found;
    size_t          bestStart, bestEnd;
    unsigned*       seen;       /* per pc: the generation in which it was added */
    unsigned        generation;
    int*            stack;
    size_t          stackCapacity;
    int             nomem;
} Pike;

/** Adds the instruction and what it leads to without consuming, at position pos. */
static void add_thread(Pike* v, ThreadList* list, int pc0, size_t start, size_t pos) {
    size_t top = 0;
    const Regex* re = v->re;

    if (VEC_RESERVE(v->stack, v->stackCapacity, 1) != 0) {
        v->nomem = 1;
        return;
    }
    v->stack[top++] = pc0;

    while (top > 0) {
        int pc = v->stack[--top];
        const Inst* in;

        for (;;) {
            if (v->seen[pc] == v->generation)
                break;
            v->seen[pc] = v->generation;
            in = &re->program[pc];

            if (in->op == I_JMP) {
                pc = in->x;
                continue;
            }
            if (in->op == I_SPLIT) {
                if (VEC_RESERVE(v->stack, v->stackCapacity, top + 1) != 0) {
                    v->nomem = 1;
                    return;
                }
                v->stack[top++] = in->y;
                pc = in->x;
                continue;
            }
            if (in->op == I_SAVE) {
                pc++;
                continue;
            }
            if (in->op == I_ASSERT) {
                if (!assertion_holds((AssertKind)in->x, v->t, v->n, pos))
                    break;
                pc++;
                continue;
            }
            if (in->op == I_MATCH) {
                if (!v->found || start < v->bestStart || (start == v->bestStart && pos > v->bestEnd)) {
                    v->found = 1;
                    v->bestStart = start;
                    v->bestEnd = pos;
                }
                break;
            }
            if (in->op == I_FAIL)
                break;

            // a consuming instruction waits for the next character
            list->threads[list->count].pc = pc;
            list->threads[list->count].start = start;
            list->count++;
            break;
        }
    }
}

static int search_automaton(const Regex* re, const uint32_t* t, size_t n, size_t from,
                            size_t* start, size_t* end) {
    Pike v;
    ThreadList current, next, tmp;
    size_t pos;
    int result = 0;

    memset(&v, 0, sizeof v);
    v.re = re;
    v.t = t;
    v.n = n;
    v.seen = calloc(re->length + 1, sizeof *v.seen);
    current.threads = malloc((re->length + 1) * sizeof(Thread));
    next.threads = malloc((re->length + 1) * sizeof(Thread));
    current.count = next.count = 0;
    if (v.seen == NULL || current.threads == NULL || next.threads == NULL)
        goto done;

    for (pos = from;; pos++) {
        size_t k;

        // a new thread starts here, after the older ones in rank: the earlier start wins
        v.generation++;
        for (k = 0; k < current.count; k++)
            v.seen[current.threads[k].pc] = v.generation;
        if (!v.found)
            add_thread(&v, &current, 0, pos, pos);
        if (v.nomem)
            goto done;

        if (current.count == 0 && (v.found || pos >= n))
            break;
        if (pos >= n)
            break;

        // step over t[pos]
        v.generation++;
        next.count = 0;
        for (k = 0; k < current.count; k++) {
            const Thread* th = &current.threads[k];
            if (v.found && th->start > v.bestStart)
                continue;
            if (consume_ok(re, &re->program[th->pc], t[pos]))
                add_thread(&v, &next, th->pc + 1, th->start, pos + 1);
        }
        if (v.nomem)
            goto done;

        tmp = current;
        current = next;
        next = tmp;
    }

    if (v.found) {
        *start = v.bestStart;
        *end = v.bestEnd;
        result = 1;
    }

done:
    free(v.seen);
    free(v.stack);
    free(current.threads);
    free(next.threads);
    return result;
}

/* the backtracking matcher, for expressions with back references */
typedef struct {
    const Regex*    re;
    const uint32_t* t;
    size_t          n;
    size_t*         caps;
    size_t          groups;
    int             found;
    size_t          bestEnd;
    long            steps;
} Backtrack;

static void backtrack(Backtrack* b, int pc, size_t pos) {
    const Regex* re = b->re;

    for (;;) {
        const Inst* in = &re->program[pc];

        if (++b->steps > BACKTRACK_STEPS)
            return;

        switch (in->op) {
            case I_MATCH:
                if (!b->found || pos > b->bestEnd) {
                    b->found = 1;
                    b->bestEnd = pos;
                }
                return;
            case I_FAIL:
                return;
            case I_JMP:
                pc = in->x;
                break;
            case I_SPLIT:
                backtrack(b, in->x, pos);
                pc = in->y;
                break;
            case I_SAVE: {
                size_t old = b->caps[in->x];
                b->caps[in->x] = pos;
                backtrack(b, pc + 1, pos);
                b->caps[in->x] = old;
                return;
            }
            case I_ASSERT:
                if (!assertion_holds((AssertKind)in->x, b->t, b->n, pos))
                    return;
                pc++;
                break;
            case I_BACK: {
                size_t from = b->caps[in->x * 2], to = b->caps[in->x * 2 + 1], k;
                if (from == (size_t)-1 || to == (size_t)-1 || to < from)
                    return;
                if (pos + (to - from) > b->n)
                    return;
                for (k = 0; k < to - from; k++) {
                    uint32_t a = b->t[from + k], c = b->t[pos + k];
                    if (re->icase) {
                        a = fold_char(a);
                        c = fold_char(c);
                    }
                    if (a != c)
                        return;
                }
                pos += to - from;
                pc++;
                break;
            }
            default:        // consuming
                if (pos >= b->n || !consume_ok(re, in, b->t[pos]))
                    return;
                pos++;
                pc++;
                break;
        }
    }
}

static int search_backtracking(const Regex* re, const uint32_t* t, size_t n, size_t from,
                               size_t* start, size_t* end) {
    Backtrack b;
    size_t s, slots = ((size_t)re->groupCount + 1) * 2, k;

    memset(&b, 0, sizeof b);
    b.re = re;
    b.t = t;
    b.n = n;
    b.caps = malloc(slots * sizeof *b.caps);
    if (b.caps == NULL)
        return 0;

    for (s = from; s <= n; s++) {
        for (k = 0; k < slots; k++)
            b.caps[k] = (size_t)-1;
        b.found = 0;
        b.steps = 0;
        backtrack(&b, 0, s);
        if (b.found) {
            *start = s;
            *end = b.bestEnd;
            free(b.caps);
            return 1;
        }
    }
    free(b.caps);
    return 0;
}

int regex_search(const Regex* regex, const RegexInput* input, size_t from, size_t* start, size_t* end) {
    if (from > input->length)
        return 0;
    if (regex->hasBackref)
        return search_backtracking(regex, input->chars, input->length, from, start, end);
    return search_automaton(regex, input->chars, input->length, from, start, end);
}

/* ---- compiling ------------------------------------------------------------------ */

static void copy_message(char* error, size_t size, const char* message) {
    if (error != NULL && size > 0)
        snprintf(error, size, "%s", message);
}

void regex_free(Regex* regex) {
    size_t k;
    if (regex == NULL)
        return;
    for (k = 0; k < regex->setCount; k++)
        free(regex->sets[k].ranges);
    free(regex->sets);
    free(regex->program);
    free(regex);
}

Regex* regex_compile(const char* const* patterns, size_t count, int flags, char* error, size_t errorSize) {
    Parser p;
    Regex* re = NULL;
    Node* tree = NULL;
    Emitter e;
    size_t k;
    int groupBase = 0, status = 0;

    memset(&p, 0, sizeof p);
    memset(&e, 0, sizeof e);
    p.extended = (flags & REGEX_EXTENDED) != 0;
    p.icase = (flags & REGEX_ICASE) != 0;

    if (setjmp(p.failure) != 0) {
        copy_message(error, errorSize, p.nomem ? "Memory exhausted" : p.message);
        status = -1;
        goto cleanup;
    }

    tree = new_node(&p, N_NONE);        // no pattern at all matches nothing

    for (k = 0; k < count; k++) {
        const unsigned char* bytes = (const unsigned char*)patterns[k];
        size_t length = strlen(patterns[k]), i = 0, n = 0;
        Node* one;

        if (VEC_RESERVE(p.chars, p.charCapacity, length + 1) != 0) {
            p.nomem = 1;
            fail(&p, "Memory exhausted");
        }
        while (i < length) {
            size_t used;
            p.chars[n++] = decode_char(bytes + i, length - i, &used);
            i += used;
        }

        p.pat = p.chars;
        p.length = n;
        p.i = 0;
        p.groupBase = groupBase;
        p.groupsHere = 0;
        memset(p.closedHere, 0, sizeof p.closedHere);

        if (flags & REGEX_FIXED) {
            size_t j;
            one = new_node(&p, N_EMPTY);
            for (j = 0; j < n; j++)
                one = make_cat(&p, one, make_char(&p, p.chars[j]));
        } else {
            one = parse_pattern(&p);
            groupBase += p.groupsHere;
        }

        if (k == 0) {
            tree = one;
        } else {
            Node* alt = new_node(&p, N_ALT);
            alt->a = tree;
            alt->b = one;
            tree = alt;
        }
    }

    if (flags & REGEX_LINE) {
        tree = make_cat(&p, make_cat(&p, make_assert(&p, A_BOL), tree), make_assert(&p, A_EOL));
    } else if (flags & REGEX_WORD) {
        tree = make_cat(&p, make_cat(&p, make_assert(&p, A_NOT_WORD_BEFORE), tree),
                        make_assert(&p, A_NOT_WORD_AFTER));
    }

    e.withSaves = p.hasBackref;
    emit_node(&e, tree);
    emit(&e, I_MATCH, 0, 0, 0);
    if (e.tooBig) {
        copy_message(error, errorSize, "Regular expression too big");
        status = -1;
        goto cleanup;
    }

    re = calloc(1, sizeof *re);
    if (re == NULL) {
        copy_message(error, errorSize, "Memory exhausted");
        status = -1;
        goto cleanup;
    }
    re->program = e.code;
    re->length = e.length;
    re->sets = p.sets;
    re->setCount = p.setCount;
    re->groupCount = groupBase;
    re->hasBackref = p.hasBackref;
    re->icase = p.icase;
    e.code = NULL;
    p.sets = NULL;
    p.setCount = 0;

cleanup:
    for (k = 0; k < p.nodeCount; k++)
        free(p.nodes[k]);
    free(p.nodes);
    free(p.chars);
    free(e.code);
    for (k = 0; k < p.setCount; k++)
        free(p.sets[k].ranges);
    free(p.sets);
    (void)status;
    return re;
}
