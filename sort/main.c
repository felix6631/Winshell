#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"
#include "vpath.h"
#include "args.h"
#include "vec.h"

#define VERSION "0.1"
#define PROGRAM "sort"

/*
 * sort: lines are compared as bytes, as GNU sort does in the C locale
 * (LC_ALL=C), so the order does not depend on the settings of a machine.
 * A CR in front of the line feed is part of the line ending: "b\r\n" and
 * "b\n" are the same line to every comparison, and the ending of each line
 * is written back as it was.
 */

static void print_usage(void) {
    puts("Usage: sort [OPTION]... [FILE]...\n"
         "Write sorted concatenation of all FILE(s) to standard output.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "Ordering options:\n"
         "  -b, --ignore-leading-blanks  ignore leading blanks\n"
         "  -d, --dictionary-order       consider only blanks and alphanumeric characters\n"
         "  -f, --ignore-case            fold lower case to upper case characters\n"
         "  -g, --general-numeric-sort   compare according to general numerical value\n"
         "  -i, --ignore-nonprinting     consider only printable characters\n"
         "  -n, --numeric-sort           compare according to string numerical value\n"
         "  -r, --reverse                reverse the result of comparisons\n"
         "\n"
         "Other options:\n"
         "  -c, --check, -C              check for sorted input; do not sort\n"
         "  -k, --key=KEYDEF             sort via a key; KEYDEF gives location and type\n"
         "  -m, --merge                  accepted; the input is sorted as a whole\n"
         "  -o, --output=FILE            write result to FILE instead of standard output\n"
         "  -s, --stable                 stabilize sort by disabling last-resort comparison\n"
         "  -t, --field-separator=SEP    use SEP instead of non-blank to blank transition\n"
         "  -u, --unique                 output only the first of lines that compare equal\n"
         "  -S, -T, --parallel           accepted and ignored\n"
         "      --help                   display this help and exit\n"
         "      --version                output version information and exit\n"
         "\n"
         "KEYDEF is F[.C][OPTS][,F[.C][OPTS]] for start and stop position, where F is a\n"
         "field number and C a character position in the field, both counted from 1;\n"
         "OPTS is one or more of bdfgin and r, which override global ordering options.");
}

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

typedef struct {
    size_t startField, startChar;       // 0 based
    size_t endField, endChar;           // endField 0 based; endChar 0: to the end of the field
    int    hasEnd;
    int    blankStart, blankEnd;
    int    dict, fold, ignore, numeric, general, reverse;
    int    hasOptions;
} Key;

/** What the order of the lines depends on: the keys and the options that apply to all of them. */
typedef struct {
    Key*   keys;
    size_t keyCount;
    int    tabSeparator;                // -1: transition from non-blank to blank
    int    unique, stable;
    int    reverse;                     // the last resort comparison of the whole line goes the other way
} Order;

typedef struct {
    TextLine line;
    size_t   index;                     // position in the input, the last resort of a stable sort
} Entry;

static int is_blank(int c) {
    return c == ' ' || c == '\t';
}

/* ---- keys ------------------------------------------------------------------ */

static const unsigned char* begin_of_key(const TextLine* line, const Key* key, int tabSeparator) {
    const unsigned char* p = line->content;
    const unsigned char* limit = line->content + line->length;
    size_t n;

    if (tabSeparator >= 0) {
        for (n = key->startField; n > 0 && p < limit; n--) {
            while (p < limit && *p != (unsigned char)tabSeparator)
                p++;
            if (p < limit)
                p++;
        }
    } else {
        for (n = key->startField; n > 0 && p < limit; n--) {
            while (p < limit && is_blank(*p))
                p++;
            while (p < limit && !is_blank(*p))
                p++;
        }
    }

    if (key->blankStart)
        while (p < limit && is_blank(*p))
            p++;

    if ((size_t)(limit - p) > key->startChar)
        p += key->startChar;
    else
        p = limit;
    return p;
}

static const unsigned char* end_of_key(const TextLine* line, const Key* key, int tabSeparator) {
    const unsigned char* p = line->content;
    const unsigned char* limit = line->content + line->length;
    size_t fields, n;

    if (!key->hasEnd)
        return limit;

    fields = key->endField;
    if (key->endChar == 0)
        fields++;                       // the whole of the last field

    n = fields;
    if (tabSeparator >= 0) {
        while (p < limit && n > 0) {
            n--;
            while (p < limit && *p != (unsigned char)tabSeparator)
                p++;
            if (p < limit && (n > 0 || key->endChar > 0))
                p++;
        }
    } else {
        while (p < limit && n > 0) {
            n--;
            while (p < limit && is_blank(*p))
                p++;
            while (p < limit && !is_blank(*p))
                p++;
        }
    }

    if (key->endChar > 0) {
        if (key->blankEnd)
            while (p < limit && is_blank(*p))
                p++;
        if ((size_t)(limit - p) > key->endChar)
            p += key->endChar;
        else
            p = limit;
    }
    return p;
}

/* ---- comparing ---------------------------------------------------------------- */

static int next_significant(const unsigned char** p, const unsigned char* end, const Key* key) {
    while (*p < end) {
        int c = *(*p)++;
        if (key->dict && !isalnum(c) && !is_blank(c))
            continue;
        if (key->ignore && (c < 32 || c > 126))
            continue;
        return key->fold ? toupper(c) : c;
    }
    return -1;
}

static int compare_text(const unsigned char* a, const unsigned char* aEnd,
                        const unsigned char* b, const unsigned char* bEnd, const Key* key) {
    if (!key->dict && !key->ignore && !key->fold) {
        size_t la = (size_t)(aEnd - a), lb = (size_t)(bEnd - b);
        int result = memcmp(a, b, la < lb ? la : lb);
        if (result != 0)
            return result < 0 ? -1 : 1;
        return la < lb ? -1 : (la > lb ? 1 : 0);
    }
    for (;;) {
        int ca = next_significant(&a, aEnd, key), cb = next_significant(&b, bEnd, key);
        if (ca < 0 || cb < 0)
            return ca < 0 ? (cb < 0 ? 0 : -1) : 1;
        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
}

/** A number as sort -n reads it: blanks, a sign, digits, a fraction; what follows is ignored. */
typedef struct {
    int                  sign;              // 1 or -1; zero is always 1
    const unsigned char* integer;           // without leading zeros
    size_t               integerLength;
    const unsigned char* fraction;          // without trailing zeros
    size_t               fractionLength;
} Number;

static void read_number(const unsigned char* p, const unsigned char* end, Number* n) {
    n->sign = 1;
    n->fractionLength = 0;

    while (p < end && is_blank(*p))
        p++;
    if (p < end && *p == '-') {
        n->sign = -1;
        p++;
    }
    while (p < end && *p == '0')
        p++;

    n->integer = p;
    while (p < end && *p >= '0' && *p <= '9')
        p++;
    n->integerLength = (size_t)(p - n->integer);

    n->fraction = p;
    if (p < end && *p == '.') {
        n->fraction = ++p;
        while (p < end && *p >= '0' && *p <= '9')
            p++;
        n->fractionLength = (size_t)(p - n->fraction);
        while (n->fractionLength > 0 && n->fraction[n->fractionLength - 1] == '0')
            n->fractionLength--;
    }
    if (n->integerLength == 0 && n->fractionLength == 0)
        n->sign = 1;
}

static int compare_magnitudes(const Number* x, const Number* y) {
    size_t common;
    int c;

    if (x->integerLength != y->integerLength)
        return x->integerLength < y->integerLength ? -1 : 1;
    c = x->integerLength > 0 ? memcmp(x->integer, y->integer, x->integerLength) : 0;
    if (c != 0)
        return c < 0 ? -1 : 1;

    common = x->fractionLength < y->fractionLength ? x->fractionLength : y->fractionLength;
    c = common > 0 ? memcmp(x->fraction, y->fraction, common) : 0;
    if (c != 0)
        return c < 0 ? -1 : 1;
    if (x->fractionLength != y->fractionLength)
        return x->fractionLength < y->fractionLength ? -1 : 1;
    return 0;
}

static int compare_numeric(const unsigned char* a, const unsigned char* aEnd,
                           const unsigned char* b, const unsigned char* bEnd) {
    Number x, y;
    int result;

    read_number(a, aEnd, &x);
    read_number(b, bEnd, &y);
    if (x.sign != y.sign)
        return x.sign < y.sign ? -1 : 1;
    result = compare_magnitudes(&x, &y);
    return x.sign < 0 ? -result : result;
}

/** General numeric: the leading number as strtod reads it; text that is no number comes first. */
static int compare_general(const unsigned char* a, const unsigned char* aEnd,
                           const unsigned char* b, const unsigned char* bEnd) {
    char bufferA[128], bufferB[128];
    char *endA, *endB;
    size_t la = (size_t)(aEnd - a), lb = (size_t)(bEnd - b);
    double va, vb;
    int okA, okB;

    if (la >= sizeof bufferA)
        la = sizeof bufferA - 1;
    if (lb >= sizeof bufferB)
        lb = sizeof bufferB - 1;
    memcpy(bufferA, a, la);
    bufferA[la] = '\0';
    memcpy(bufferB, b, lb);
    bufferB[lb] = '\0';

    va = strtod(bufferA, &endA);
    vb = strtod(bufferB, &endB);
    okA = endA != bufferA;
    okB = endB != bufferB;

    if (!okA || !okB)
        return okA == okB ? 0 : (okA ? 1 : -1);
    if (va != va || vb != vb)
        return (va != va) == (vb != vb) ? 0 : (va != va ? -1 : 1);
    return va < vb ? -1 : (va > vb ? 1 : 0);
}

static int compare_by_keys(const Order* order, const TextLine* a, const TextLine* b) {
    size_t k;
    for (k = 0; k < order->keyCount; k++) {
        const Key* key = &order->keys[k];
        const unsigned char* a0 = begin_of_key(a, key, order->tabSeparator);
        const unsigned char* a1 = end_of_key(a, key, order->tabSeparator);
        const unsigned char* b0 = begin_of_key(b, key, order->tabSeparator);
        const unsigned char* b1 = end_of_key(b, key, order->tabSeparator);
        int result;

        if (a1 < a0)
            a1 = a0;
        if (b1 < b0)
            b1 = b0;

        if (key->numeric)
            result = compare_numeric(a0, a1, b0, b1);
        else if (key->general)
            result = compare_general(a0, a1, b0, b1);
        else
            result = compare_text(a0, a1, b0, b1, key);

        if (result != 0)
            return key->reverse ? -result : result;
    }
    return 0;
}

/** The whole order: the keys, then the whole line as the last resort. */
static int compare_entries(const Order* order, const Entry* x, const Entry* y) {
    int result = compare_by_keys(order, &x->line, &y->line);
    if (result != 0)
        return result;
    if (order->unique || order->stable)
        return 0;

    {
        size_t la = x->line.length, lb = y->line.length;
        int c = memcmp(x->line.content, y->line.content, la < lb ? la : lb);
        if (c == 0 && la != lb)
            c = la < lb ? -1 : 1;
        if (c != 0)
            return order->reverse ? (c < 0 ? 1 : -1) : (c < 0 ? -1 : 1);
    }
    return 0;
}

/** Stable merge sort. */
static void merge_sort(const Order* order, Entry* items, Entry* scratch, size_t count) {
    size_t mid, i, j, k;

    if (count < 2)
        return;
    mid = count / 2;
    merge_sort(order, items, scratch, mid);
    merge_sort(order, items + mid, scratch, count - mid);

    i = 0;
    j = mid;
    k = 0;
    while (i < mid && j < count) {
        if (compare_entries(order, &items[j], &items[i]) < 0)
            scratch[k++] = items[j++];
        else
            scratch[k++] = items[i++];
    }
    while (i < mid)
        scratch[k++] = items[i++];
    while (j < count)
        scratch[k++] = items[j++];
    memcpy(items, scratch, count * sizeof *items);
}

/* ---- options --------------------------------------------------------------------- */

static void apply_flag(Key* key, char flag, int* ok) {
    switch (flag) {
        case 'b': key->blankStart = key->blankEnd = 1; break;
        case 'd': key->dict = 1; break;
        case 'f': key->fold = 1; break;
        case 'g': key->general = 1; break;
        case 'i': key->ignore = 1; break;
        case 'n': key->numeric = 1; break;
        case 'r': key->reverse = 1; break;
        default:  *ok = 0; break;       // including h, M and V, which are not supported
    }
}

static int parse_key(const char* text, Key* key) {
    const char* p = text;
    char* end;
    unsigned long value;
    int ok = 1;

    memset(key, 0, sizeof *key);

    value = strtoul(p, &end, 10);
    if (end == p || value == 0)
        return -1;
    key->startField = value - 1;
    p = end;

    if (*p == '.') {
        value = strtoul(p + 1, &end, 10);
        if (end == p + 1 || value == 0)
            return -1;
        key->startChar = value - 1;
        p = end;
    }

    while (*p != '\0' && *p != ',') {
        apply_flag(key, *p, &ok);
        key->hasOptions = 1;
        p++;
    }

    if (*p == ',') {
        key->hasEnd = 1;
        p++;
        value = strtoul(p, &end, 10);
        if (end == p || value == 0)
            return -1;
        key->endField = value - 1;
        p = end;

        if (*p == '.') {
            value = strtoul(p + 1, &end, 10);
            if (end == p + 1)
                return -1;
            key->endChar = value;
            p = end;
        }
        while (*p != '\0') {
            // for the end of the key, b means to skip blanks at its start
            if (*p == 'b')
                key->blankEnd = 1;
            else
                apply_flag(key, *p, &ok);
            key->hasOptions = 1;
            p++;
        }
    }
    return ok ? 0 : -1;
}

static void add_key(Order* order, const Key* key) {
    if (VEC_GROW_BY_COUNT(order->keys, order->keyCount) != 0) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        exit(2);
    }
    order->keys[order->keyCount++] = *key;
}

/* ---- main ------------------------------------------------------------------------- */

enum { OPT_BLANKS = 1, OPT_DICT, OPT_FOLD, OPT_GENERAL, OPT_IGNORE, OPT_NUMERIC, OPT_REVERSE, OPT_STABLE,
       OPT_UNIQUE, OPT_CHECK, OPT_CHECK_QUIET, OPT_MERGE, OPT_KEY, OPT_SEPARATOR, OPT_OUTPUT, OPT_ACCEPTED,
       OPT_UNSUPPORTED_H, OPT_UNSUPPORTED_M, OPT_UNSUPPORTED_V, OPT_UNSUPPORTED_Z };

static const ArgsOption options[] = {
    { OPT_BLANKS,    'b', "ignore-leading-blanks", ARGS_NONE },
    { OPT_DICT,      'd', "dictionary-order",      ARGS_NONE },
    { OPT_FOLD,      'f', "ignore-case",           ARGS_NONE },
    { OPT_GENERAL,   'g', "general-numeric-sort",  ARGS_NONE },
    { OPT_IGNORE,    'i', "ignore-nonprinting",    ARGS_NONE },
    { OPT_NUMERIC,   'n', "numeric-sort",          ARGS_NONE },
    { OPT_REVERSE,   'r', "reverse",               ARGS_NONE },
    { OPT_STABLE,    's', "stable",                ARGS_NONE },
    { OPT_UNIQUE,    'u', "unique",                ARGS_NONE },
    { OPT_CHECK,     'c', "check",                 ARGS_NONE },
    { OPT_CHECK_QUIET, 'C', NULL,                  ARGS_NONE },
    { OPT_MERGE,     'm', "merge",                 ARGS_NONE },
    { OPT_KEY,       'k', "key",                   ARGS_REQUIRED },
    { OPT_SEPARATOR, 't', "field-separator",       ARGS_REQUIRED },
    { OPT_OUTPUT,    'o', "output",                ARGS_REQUIRED },
    { OPT_ACCEPTED,  'S', "buffer-size",           ARGS_REQUIRED },
    { OPT_ACCEPTED,  'T', "temporary-directory",   ARGS_REQUIRED },
    { OPT_ACCEPTED,  0,   "parallel",              ARGS_REQUIRED },
    { OPT_UNSUPPORTED_H, 'h', NULL,                ARGS_NONE },
    { OPT_UNSUPPORTED_M, 'M', NULL,                ARGS_NONE },
    { OPT_UNSUPPORTED_V, 'V', NULL,                ARGS_NONE },
    { OPT_UNSUPPORTED_Z, 'z', NULL,                ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 2, 0, 0 };

typedef struct {
    Key         global;                 // the options that keys without options of their own take
    Order       order;
    const char* outputFile;
    int         check, checkQuiet;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
        case OPT_BLANKS:   o->global.blankStart = o->global.blankEnd = 1; break;
        case OPT_DICT:     o->global.dict = 1; break;
        case OPT_FOLD:     o->global.fold = 1; break;
        case OPT_GENERAL:  o->global.general = 1; break;
        case OPT_IGNORE:   o->global.ignore = 1; break;
        case OPT_NUMERIC:  o->global.numeric = 1; break;
        case OPT_REVERSE:  o->global.reverse = 1; break;
        case OPT_STABLE:   o->order.stable = 1; break;
        case OPT_UNIQUE:   o->order.unique = 1; break;
        case OPT_CHECK:    o->check = 1; break;
        case OPT_CHECK_QUIET: o->check = 1; o->checkQuiet = 1; break;
        case OPT_MERGE:    break;       // the input is sorted as a whole, which gives the same result
        case OPT_OUTPUT:   o->outputFile = value; break;
        case OPT_ACCEPTED: break;       // accepted and ignored
        case OPT_KEY: {
            Key key;
            if (parse_key(value, &key) != 0) {
                fprintf(stderr, PROGRAM ": invalid number at field start: invalid count at start of '%s'\n", value);
                return -1;
            }
            add_key(&o->order, &key);
            break;
        }
        case OPT_SEPARATOR:
            if (strcmp(value, "\\0") == 0) {
                o->order.tabSeparator = 0;
            } else if (value[0] != '\0' && value[1] == '\0') {
                o->order.tabSeparator = (unsigned char)value[0];
            } else {
                fprintf(stderr, PROGRAM ": multi-character tab '%s'\n", value);
                return -1;
            }
            break;
        case OPT_UNSUPPORTED_H: fprintf(stderr, PROGRAM ": option -h is not supported\n"); return -1;
        case OPT_UNSUPPORTED_M: fprintf(stderr, PROGRAM ": option -M is not supported\n"); return -1;
        case OPT_UNSUPPORTED_V: fprintf(stderr, PROGRAM ": option -V is not supported\n"); return -1;
        case OPT_UNSUPPORTED_Z: fprintf(stderr, PROGRAM ": option -z is not supported\n"); return -1;
    }
    return 0;
}

/**
 * Keys without options of their own use the global ones; with no key the whole line is one.
 */
static void complete_keys(Order* order, const Key* global) {
    size_t k;

    if (order->keyCount == 0) {
        Key whole = *global;
        whole.startField = whole.startChar = 0;
        whole.hasEnd = 0;
        add_key(order, &whole);
        return;
    }

    for (k = 0; k < order->keyCount; k++) {
        if (!order->keys[k].hasOptions) {
            Key own = order->keys[k];
            own.blankStart = own.blankEnd = global->blankStart;
            own.dict = global->dict;
            own.fold = global->fold;
            own.general = global->general;
            own.ignore = global->ignore;
            own.numeric = global->numeric;
            own.reverse = global->reverse;
            order->keys[k] = own;
        }
    }
}

/**
 * Reads every input and lists its lines in *entries (the texts stay allocated: the lines point into them).
 * Returns 0, 2 when an input could not be opened (the others are read), or -1 when out of memory.
 */
static int load_lines(const char** operands, int operandCount, Entry** entries, size_t* entryCount) {
    size_t entryCapacity = 0;
    int status = 0, i;

    *entries = NULL;
    *entryCount = 0;

    for (i = 0; i < operandCount; i++) {
        FILE* in;
        unsigned char* text;
        size_t length, offset = 0;
        TextLine line;

        if (text_open(PROGRAM, operands[i], 1, &in) != 0) {
            status = 2;
            continue;
        }
        text = text_slurp(in, &length);
        if (in != stdin)
            fclose(in);
        if (text == NULL)
            return -1;

        while (text_next_line(text, length, &offset, &line)) {
            if (VEC_RESERVE(*entries, entryCapacity, *entryCount + 1) != 0)
                return -1;
            (*entries)[*entryCount].line = line;
            (*entries)[*entryCount].index = *entryCount;
            (*entryCount)++;
        }
    }
    return status;
}

/** sort -c: 0 when the lines are in order, otherwise 1 (and the first line out of order is named). */
static int check_order(const Order* order, const Entry* entries, size_t count, const char* name, int quiet) {
    size_t k;

    for (k = 1; k < count; k++) {
        int result = compare_entries(order, &entries[k - 1], &entries[k]);
        if (result > 0 || (order->unique && result == 0)) {
            if (!quiet) {
                fprintf(stderr, PROGRAM ": %s:%lu: disorder: ", name, (unsigned long)(k + 1));
                fwrite(entries[k].line.content, 1, entries[k].line.length, stderr);
                fputc('\n', stderr);
            }
            return 1;
        }
    }
    return 0;
}

/** Writes the sorted lines to the output file, or to standard output; -u leaves out repeated ones. */
static int write_lines(const Order* order, const Entry* entries, size_t count, const char* outputFile) {
    FILE* out = stdout;
    size_t k;

    if (outputFile != NULL) {
        char native[VPATH_MAX];
        if (vpath_to_native(outputFile, native, sizeof native) != 0 || (out = fopen(native, "wb")) == NULL) {
            fprintf(stderr, PROGRAM ": open failed: %s: No such file or directory\n", outputFile);
            return 2;
        }
    }

    for (k = 0; k < count; k++) {
        if (order->unique && k > 0 && compare_by_keys(order, &entries[k - 1].line, &entries[k].line) == 0)
            continue;
        if (text_write_line(&entries[k].line, out) != 0) {
            fprintf(stderr, PROGRAM ": write error\n");
            return 2;
        }
    }

    if (out != stdout)
        fclose(out);
    else
        fflush(stdout);
    return 0;
}

int main(int argc, char* argv[]) {
    Options o;
    const char** operands;
    Entry* entries;
    size_t entryCount;
    int operandCount, status, result;

    memset(&o, 0, sizeof o);
    o.order.tabSeparator = -1;
    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &operandCount);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    complete_keys(&o.order, &o.global);
    o.order.reverse = o.global.reverse;

    text_binary_streams();
    if (operandCount == 0)
        operands[operandCount++] = "-";

    status = load_lines(operands, operandCount, &entries, &entryCount);
    if (status < 0) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return 2;
    }

    if (o.check)
        return status != 0 ? 2 : check_order(&o.order, entries, entryCount, operands[0], o.checkQuiet);

    if (entryCount > 1) {
        Entry* scratch = malloc(entryCount * sizeof *scratch);
        if (scratch == NULL) {
            fprintf(stderr, PROGRAM ": memory exhausted\n");
            return 2;
        }
        merge_sort(&o.order, entries, scratch, entryCount);
        free(scratch);
    }

    result = write_lines(&o.order, entries, entryCount, o.outputFile);
    return result != 0 ? result : status;
}
