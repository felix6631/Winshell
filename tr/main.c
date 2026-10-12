#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"
#include "args.h"
#include "vec.h"

#define VERSION "0.1"
#define PROGRAM "tr"

/*
 * tr works on bytes, as GNU tr does in the C locale. Standard input and
 * output are binary, so nothing happens to line endings: tr '\n' ' ' leaves
 * the CR of a CR LF file where it is, and tr -d '\r' turns a CR LF file into
 * an LF file.
 */

static void print_usage(void) {
    puts("Usage: tr [OPTION]... SET1 [SET2]\n"
         "Translate, squeeze, and/or delete characters from standard input,\n"
         "writing to standard output.\n"
         "\n"
         "  -c, -C, --complement    use the complement of SET1\n"
         "  -d, --delete            delete characters in SET1, do not translate\n"
         "  -s, --squeeze-repeats   replace each sequence of a repeated character\n"
         "                            that is listed in the last specified SET,\n"
         "                            with a single occurrence of that character\n"
         "  -t, --truncate-set1     first truncate SET1 to length of SET2\n"
         "      --help              display this help and exit\n"
         "      --version           output version information and exit\n"
         "\n"
         "SETs are specified as strings of characters. Interpreted sequences are:\n"
         "  \\NNN            character with octal value NNN (1 to 3 octal digits)\n"
         "  \\\\              backslash\n"
         "  \\a \\b \\f \\n \\r \\t \\v   audible BEL, backspace, form feed, new line,\n"
         "                  return, horizontal tab, vertical tab\n"
         "  CHAR1-CHAR2     all characters from CHAR1 to CHAR2 in ascending order\n"
         "  [CHAR*]         in SET2, copies of CHAR until length of SET1\n"
         "  [CHAR*REPEAT]   REPEAT copies of CHAR, REPEAT octal if starting with 0\n"
         "  [:alnum:] [:alpha:] [:blank:] [:cntrl:] [:digit:] [:graph:] [:lower:]\n"
         "  [:print:] [:punct:] [:space:] [:upper:] [:xdigit:]   character classes\n"
         "  [=CHAR=]        all characters which are equivalent to CHAR");
}

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

typedef struct {
    unsigned char* bytes;
    size_t         length;
    size_t*        fillAt;      // positions of [c*] constructs that are filled up later
    unsigned char* fillChar;
    size_t         fills;
} Set;

static int add_byte(Set* set, size_t* capacity, unsigned char c) {
    if (VEC_RESERVE(set->bytes, *capacity, set->length + 1) != 0)
        return -1;
    set->bytes[set->length++] = c;
    return 0;
}

/** Reads one character of a set, resolving a backslash escape. Returns -1 at the end of the string. */
static int read_char(const char** p) {
    int c = (unsigned char)**p;

    if (c == '\0')
        return -1;
    (*p)++;
    if (c != '\\')
        return c;

    c = (unsigned char)**p;
    switch (c) {
        case '\0': return '\\';         // a trailing backslash stands for itself
        case 'a': (*p)++; return '\a';
        case 'b': (*p)++; return '\b';
        case 'f': (*p)++; return '\f';
        case 'n': (*p)++; return '\n';
        case 'r': (*p)++; return '\r';
        case 't': (*p)++; return '\t';
        case 'v': (*p)++; return '\v';
        case '\\': (*p)++; return '\\';
        default:
            if (c >= '0' && c <= '7') {
                int value = 0, digits = 0;
                while (digits < 3 && **p >= '0' && **p <= '7') {
                    value = value * 8 + (**p - '0');
                    (*p)++;
                    digits++;
                }
                return value & 0xFF;
            }
            (*p)++;
            return c;                   // an unknown escape is the character itself
    }
}

static int in_class(const char* name, size_t length, int c) {
    #define IS(text, test) if (length == sizeof(text) - 1 && memcmp(name, text, length) == 0) return test
    IS("alnum", isalnum(c) != 0);
    IS("alpha", isalpha(c) != 0);
    IS("blank", c == ' ' || c == '\t');
    IS("cntrl", iscntrl(c) != 0);
    IS("digit", isdigit(c) != 0);
    IS("graph", isgraph(c) != 0);
    IS("lower", islower(c) != 0);
    IS("print", isprint(c) != 0);
    IS("punct", ispunct(c) != 0);
    IS("space", isspace(c) != 0);
    IS("upper", isupper(c) != 0);
    IS("xdigit", isxdigit(c) != 0);
    #undef IS
    return -1;
}

/**
 * Expands a set string. isSecond allows [c*n] constructs; the ones without a
 * count are recorded and filled when the length of the first set is known.
 */
static int expand_set(const char* text, int isSecond, Set* set) {
    const char* p = text;
    size_t capacity = 0;

    memset(set, 0, sizeof *set);

    while (*p != '\0') {
        // [:class:], [=c=], [c*n]
        if (p[0] == '[' && p[1] == ':') {
            const char* end = strstr(p + 2, ":]");
            if (end != NULL) {
                int c, known = 0;
                for (c = 0; c < 256 && !known; c++)
                    if (in_class(p + 2, (size_t)(end - (p + 2)), c) >= 0)
                        known = 1;
                if (!known) {
                    fprintf(stderr, PROGRAM ": invalid character class '%.*s'\n", (int)(end - (p + 2)), p + 2);
                    return -1;
                }
                for (c = 0; c < 256; c++)
                    if (in_class(p + 2, (size_t)(end - (p + 2)), c) > 0)
                        add_byte(set, &capacity, (unsigned char)c);
                p = end + 2;
                continue;
            }
        } else if (p[0] == '[' && p[1] == '=' && p[2] != '\0' && p[3] == '=' && p[4] == ']') {
            add_byte(set, &capacity, (unsigned char)p[2]);
            p += 5;
            continue;
        } else if (p[0] == '[') {
            const char* q = p + 1;
            int c = read_char(&q);
            if (c >= 0 && *q == '*') {
                const char* close = strchr(q + 1, ']');
                if (close != NULL) {
                    const char* count = q + 1;
                    int digitsOnly = 1;
                    const char* d;
                    for (d = count; d < close; d++)
                        if (*d < '0' || *d > '9')
                            digitsOnly = 0;
                    if (digitsOnly) {
                        if (!isSecond) {
                            fprintf(stderr, PROGRAM ": the [c*] repeat construct may not appear in string1\n");
                            return -1;
                        }
                        if (close == count) {
                            // [c*]: as many as the first set needs
                            if (VEC_GROW_BY_COUNT(set->fillAt, set->fills) != 0 ||
                                VEC_GROW_BY_COUNT(set->fillChar, set->fills) != 0)
                                return -1;
                            set->fillAt[set->fills] = set->length;
                            set->fillChar[set->fills] = (unsigned char)c;
                            set->fills++;
                        } else {
                            unsigned long n = strtoul(count, NULL, count[0] == '0' ? 8 : 10);
                            while (n-- > 0)
                                add_byte(set, &capacity, (unsigned char)c);
                        }
                        p = close + 1;
                        continue;
                    }
                }
            }
        }

        {
            int first = read_char(&p);
            // a range
            if (*p == '-' && p[1] != '\0') {
                const char* q = p + 1;
                int last = read_char(&q);
                if (last < first) {
                    fprintf(stderr, PROGRAM ": range-endpoints of '%c-%c' are in reverse collating sequence order\n",
                            first, last);
                    return -1;
                }
                for (; first <= last; first++)
                    add_byte(set, &capacity, (unsigned char)first);
                p = q;
            } else {
                add_byte(set, &capacity, (unsigned char)first);
            }
        }
    }
    return 0;
}

/** Puts the bytes of the [c*] constructs in, so that the set is at least `needed` long. */
static void fill_set(Set* set, size_t needed) {
    size_t i, extra, perFill, rest;
    unsigned char* bytes;
    size_t length = 0, source = 0;

    if (set->fills == 0 || needed <= set->length)
        return;

    extra = needed - set->length;
    perFill = extra / set->fills;
    rest = extra % set->fills;       // the first construct takes what does not divide

    bytes = malloc(needed + 1);
    if (bytes == NULL)
        return;

    for (i = 0; i <= set->fills; i++) {
        size_t until = i < set->fills ? set->fillAt[i] : set->length;
        while (source < until)
            bytes[length++] = set->bytes[source++];
        if (i < set->fills) {
            size_t n = perFill + (i == 0 ? rest : 0), k;
            for (k = 0; k < n; k++)
                bytes[length++] = set->fillChar[i];
        }
    }
    free(set->bytes);
    set->bytes = bytes;
    set->length = length;
}

enum { OPT_COMPLEMENT = 1, OPT_C_UPPER, OPT_DELETE, OPT_SQUEEZE, OPT_TRUNCATE };

static const ArgsOption options[] = {
    { OPT_COMPLEMENT, 'c', "complement",        ARGS_NONE },
    { OPT_COMPLEMENT, 'C', NULL,                ARGS_NONE },
    { OPT_DELETE,     'd', "delete",            ARGS_NONE },
    { OPT_SQUEEZE,    's', "squeeze-repeats",   ARGS_NONE },
    { OPT_TRUNCATE,   't', "truncate-set1",     ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int complement, deleteMode, squeeze, truncate;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;
    (void)value;
    switch (id) {
        case OPT_COMPLEMENT: o->complement = 1; break;
        case OPT_DELETE:     o->deleteMode = 1; break;
        case OPT_SQUEEZE:    o->squeeze = 1; break;
        case OPT_TRUNCATE:   o->truncate = 1; break;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    Options o = { 0, 0, 0, 0 };
    const char** operands;
    int operandCount, complement, deleteMode, squeeze, truncate_, result;
    Set set1, set2;
    unsigned char map[256];
    unsigned char inDelete[256], inSqueeze[256];
    int c, last = -1;
    unsigned char buffer[65536];
    unsigned char out[65536];
    size_t got, used = 0, j;

    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &operandCount);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    complement = o.complement;
    deleteMode = o.deleteMode;
    squeeze = o.squeeze;
    truncate_ = o.truncate;

    if (operandCount > 2) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[2]);
        try_help();
        return 1;
    }

    if (operandCount == 0) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        try_help();
        return 1;
    }
    if (operandCount == 1 && !deleteMode && !squeeze) {
        fprintf(stderr, PROGRAM ": missing operand after '%s'\nTwo strings must be given when translating.\n",
                operands[0]);
        try_help();
        return 1;
    }
    if (operandCount == 2 && deleteMode && !squeeze) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\nOnly one string may be given when deleting without "
                        "squeezing repeats.\n", operands[1]);
        try_help();
        return 1;
    }
    if (operandCount == 1 && deleteMode && squeeze) {
        fprintf(stderr, PROGRAM ": missing operand after '%s'\nTwo strings must be given when both deleting "
                        "and squeezing repeats.\n", operands[0]);
        try_help();
        return 1;
    }

    if (expand_set(operands[0], 0, &set1) != 0)
        return 1;
    memset(&set2, 0, sizeof set2);
    if (operandCount == 2 && expand_set(operands[1], 1, &set2) != 0)
        return 1;

    // the first set as the list of bytes it stands for; the complement is in ascending order
    if (complement) {
        unsigned char member[256];
        size_t n = 0;
        memset(member, 0, sizeof member);
        for (j = 0; j < set1.length; j++)
            member[set1.bytes[j]] = 1;
        free(set1.bytes);
        set1.bytes = malloc(256);
        for (c = 0; c < 256; c++)
            if (!member[c])
                set1.bytes[n++] = (unsigned char)c;
        set1.length = n;
    }

    for (c = 0; c < 256; c++)
        map[c] = (unsigned char)c;
    memset(inDelete, 0, sizeof inDelete);
    memset(inSqueeze, 0, sizeof inSqueeze);

    if (deleteMode) {
        for (j = 0; j < set1.length; j++)
            inDelete[set1.bytes[j]] = 1;
        if (squeeze)
            for (j = 0; j < set2.length; j++)
                inSqueeze[set2.bytes[j]] = 1;
    } else if (operandCount == 2) {
        // translating
        fill_set(&set2, set1.length);
        if (truncate_ && set1.length > set2.length)
            set1.length = set2.length;
        if (set2.length == 0 && set1.length > 0) {
            fprintf(stderr, PROGRAM ": when not truncating set1, string2 must be non-empty\n");
            return 1;
        }
        for (j = 0; j < set1.length; j++) {
            unsigned char to = j < set2.length ? set2.bytes[j] : set2.bytes[set2.length - 1];
            map[set1.bytes[j]] = to;
        }
        if (squeeze)
            for (j = 0; j < set2.length; j++)
                inSqueeze[set2.bytes[j]] = 1;
    } else {
        // squeezing only
        for (j = 0; j < set1.length; j++)
            inSqueeze[set1.bytes[j]] = 1;
    }

    text_binary_streams();

    // what is there is translated and written at once, so that it works in the middle of a pipeline
    // of programs that produce their output slowly
    for (;;) {
        long read = text_read_some(stdin, buffer, sizeof buffer);
        if (read <= 0)
            break;
        got = (size_t)read;

        for (j = 0; j < got; j++) {
            unsigned char b = buffer[j];

            if (deleteMode && inDelete[b])
                continue;
            b = map[b];
            if (squeeze && inSqueeze[b] && last == b)
                continue;
            last = b;

            out[used++] = b;
            if (used == sizeof out) {
                if (fwrite(out, 1, used, stdout) != used)
                    return 1;
                used = 0;
            }
        }
        if (used > 0) {
            if (fwrite(out, 1, used, stdout) != used)
                return 1;
            used = 0;
        }
        fflush(stdout);
    }
    if (used > 0 && fwrite(out, 1, used, stdout) != used)
        return 1;
    fflush(stdout);
    return 0;
}
