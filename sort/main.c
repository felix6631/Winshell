#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"
#include "vpath.h"

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

static Key* keys;
static size_t keyCount;
static int tabSeparator = -1;           // -1: transition from non-blank to blank
static int unique, stable;

typedef struct {
    TextLine line;
    size_t   index;                     // position in the input, the last resort of a stable sort
} Entry;

static int is_blank(int c) {
    return c == ' ' || c == '\t';
}

/* ---- keys ------------------------------------------------------------------ */

static const unsigned char* begin_of_key(const TextLine* line, const Key* key) {
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

static const unsigned char* end_of_key(const TextLine* line, const Key* key) {
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

/** Compares by string numerical value: optional blanks, a sign, digits with a decimal point. */
static int compare_numeric(const unsigned char* a, const unsigned char* aEnd,
                           const unsigned char* b, const unsigned char* bEnd) {
    int signA = 1, signB = 1;
    const unsigned char *ia, *ib, *fa, *fb;
    size_t ila, ilb, fla, flb, i;

    while (a < aEnd && is_blank(*a))
        a++;
    while (b < bEnd && is_blank(*b))
        b++;
    if (a < aEnd && *a == '-') {
        signA = -1;
        a++;
    }
    if (b < bEnd && *b == '-') {
        signB = -1;
        b++;
    }

    // integer part without leading zeros, then the fraction without trailing zeros
    while (a < aEnd && *a == '0')
        a++;
    while (b < bEnd && *b == '0')
        b++;
    ia = a;
    while (a < aEnd && *a >= '0' && *a <= '9')
        a++;
    ila = (size_t)(a - ia);
    ib = b;
    while (b < bEnd && *b >= '0' && *b <= '9')
        b++;
    ilb = (size_t)(b - ib);

    fa = a;
    fla = 0;
    if (a < aEnd && *a == '.') {
        fa = a + 1;
        a = fa;
        while (a < aEnd && *a >= '0' && *a <= '9')
            a++;
        fla = (size_t)(a - fa);
        while (fla > 0 && fa[fla - 1] == '0')
            fla--;
    }
    fb = b;
    flb = 0;
    if (b < bEnd && *b == '.') {
        fb = b + 1;
        b = fb;
        while (b < bEnd && *b >= '0' && *b <= '9')
            b++;
        flb = (size_t)(b - fb);
        while (flb > 0 && fb[flb - 1] == '0')
            flb--;
    }

    // zero has no sign
    if (ila == 0 && fla == 0)
        signA = 1;
    if (ilb == 0 && flb == 0)
        signB = 1;
    if (signA != signB)
        return signA < signB ? -1 : 1;

    {
        int result = 0;
        if (ila != ilb) {
            result = ila < ilb ? -1 : 1;
        } else {
            int c = ila > 0 ? memcmp(ia, ib, ila) : 0;
            if (c != 0) {
                result = c < 0 ? -1 : 1;
            } else {
                size_t common = fla < flb ? fla : flb;
                c = common > 0 ? memcmp(fa, fb, common) : 0;
                if (c != 0)
                    result = c < 0 ? -1 : 1;
                else if (fla != flb)
                    result = fla < flb ? -1 : 1;
            }
        }
        (void)i;
        return signA < 0 ? -result : result;
    }
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

static int compare_by_keys(const TextLine* a, const TextLine* b) {
    size_t k;
    for (k = 0; k < keyCount; k++) {
        const Key* key = &keys[k];
        const unsigned char* a0 = begin_of_key(a, key);
        const unsigned char* a1 = end_of_key(a, key);
        const unsigned char* b0 = begin_of_key(b, key);
        const unsigned char* b1 = end_of_key(b, key);
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

static int global_reverse;

/** The whole order: the keys, then the whole line as the last resort. */
static int compare_entries(const Entry* x, const Entry* y) {
    int result = compare_by_keys(&x->line, &y->line);
    if (result != 0)
        return result;
    if (unique || stable)
        return 0;

    {
        size_t la = x->line.length, lb = y->line.length;
        int c = memcmp(x->line.content, y->line.content, la < lb ? la : lb);
        if (c == 0 && la != lb)
            c = la < lb ? -1 : 1;
        if (c != 0)
            return global_reverse ? (c < 0 ? 1 : -1) : (c < 0 ? -1 : 1);
    }
    return 0;
}

/** Stable merge sort. */
static void merge_sort(Entry* items, Entry* scratch, size_t count) {
    size_t mid, i, j, k;

    if (count < 2)
        return;
    mid = count / 2;
    merge_sort(items, scratch, mid);
    merge_sort(items + mid, scratch, count - mid);

    i = 0;
    j = mid;
    k = 0;
    while (i < mid && j < count) {
        if (compare_entries(&items[j], &items[i]) < 0)
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

static void add_key(const Key* key) {
    Key* grown = realloc(keys, (keyCount + 1) * sizeof *keys);
    if (grown == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        exit(2);
    }
    keys = grown;
    keys[keyCount++] = *key;
}

/* ---- main ------------------------------------------------------------------------- */

int main(int argc, char* argv[]) {
    const char** operands = malloc((size_t)(argc + 1) * sizeof *operands);
    Key global;
    const char* outputFile = NULL;
    int check = 0, checkQuiet = 0, optionsEnded = 0, operandCount = 0, status = 0, i;
    unsigned char** texts = NULL;
    size_t* lengths = NULL;
    Entry* entries = NULL;
    size_t entryCount = 0, entryCapacity = 0, k;
    FILE* out = stdout;

    if (operands == NULL)
        return 2;
    memset(&global, 0, sizeof global);

    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (optionsEnded || arg[0] != '-' || arg[1] == '\0') {
            operands[operandCount++] = arg;
        } else if (strcmp(arg, "--") == 0) {
            optionsEnded = 1;
        } else if (arg[1] == '-') {
            const char* value = NULL;

            if (strcmp(arg, "--help") == 0) {
                print_usage();
                return 0;
            } else if (strcmp(arg, "--version") == 0) {
                puts(PROGRAM " (Winshell) " VERSION);
                return 0;
            } else if (strcmp(arg, "--reverse") == 0) {
                global.reverse = 1;
            } else if (strcmp(arg, "--numeric-sort") == 0) {
                global.numeric = 1;
            } else if (strcmp(arg, "--general-numeric-sort") == 0) {
                global.general = 1;
            } else if (strcmp(arg, "--ignore-case") == 0) {
                global.fold = 1;
            } else if (strcmp(arg, "--ignore-leading-blanks") == 0) {
                global.blankStart = global.blankEnd = 1;
            } else if (strcmp(arg, "--dictionary-order") == 0) {
                global.dict = 1;
            } else if (strcmp(arg, "--ignore-nonprinting") == 0) {
                global.ignore = 1;
            } else if (strcmp(arg, "--unique") == 0) {
                unique = 1;
            } else if (strcmp(arg, "--stable") == 0) {
                stable = 1;
            } else if (strcmp(arg, "--check") == 0) {
                check = 1;
            } else if (strcmp(arg, "--merge") == 0) {
                // the input is sorted as a whole, which gives the same result
            } else if (strncmp(arg, "--key=", 6) == 0) {
                value = arg + 6;
                Key key;
                if (parse_key(value, &key) != 0) {
                    fprintf(stderr, PROGRAM ": invalid key '%s'\n", value);
                    return 2;
                }
                add_key(&key);
            } else if (strncmp(arg, "--field-separator=", 18) == 0) {
                value = arg + 18;
                if (strcmp(value, "\\0") == 0)
                    tabSeparator = 0;
                else if (value[0] != '\0' && value[1] == '\0')
                    tabSeparator = (unsigned char)value[0];
                else {
                    fprintf(stderr, PROGRAM ": multi-character tab '%s'\n", value);
                    return 2;
                }
            } else if (strncmp(arg, "--output=", 9) == 0) {
                outputFile = arg + 9;
            } else if (strncmp(arg, "--parallel=", 11) == 0 || strncmp(arg, "--buffer-size=", 14) == 0 ||
                       strncmp(arg, "--temporary-directory=", 22) == 0) {
                // accepted and ignored
            } else {
                fprintf(stderr, PROGRAM ": unrecognized option '%s'\n", arg);
                try_help();
                return 2;
            }
        } else {
            const char* p;
            for (p = arg + 1; *p != '\0'; p++) {
                switch (*p) {
                    case 'b': global.blankStart = global.blankEnd = 1; break;
                    case 'd': global.dict = 1; break;
                    case 'f': global.fold = 1; break;
                    case 'g': global.general = 1; break;
                    case 'i': global.ignore = 1; break;
                    case 'n': global.numeric = 1; break;
                    case 'r': global.reverse = 1; break;
                    case 's': stable = 1; break;
                    case 'u': unique = 1; break;
                    case 'c': check = 1; break;
                    case 'C': check = 1; checkQuiet = 1; break;
                    case 'm':
                        break;      // the input is sorted as a whole, which gives the same result
                    case 'h': case 'M': case 'V': case 'z':
                        fprintf(stderr, PROGRAM ": option -%c is not supported\n", *p);
                        return 2;
                    case 'k':
                    case 't':
                    case 'o':
                    case 'S':
                    case 'T': {
                        char option = *p;
                        const char* value;

                        if (p[1] != '\0') {
                            value = p + 1;
                        } else if (i + 1 < argc) {
                            value = argv[++i];
                        } else {
                            fprintf(stderr, PROGRAM ": option requires an argument -- '%c'\n", option);
                            try_help();
                            return 2;
                        }

                        if (option == 'k') {
                            Key key;
                            if (parse_key(value, &key) != 0) {
                                fprintf(stderr, PROGRAM ": invalid number at field start: invalid count at start of '%s'\n", value);
                                return 2;
                            }
                            add_key(&key);
                        } else if (option == 't') {
                            if (strcmp(value, "\\0") == 0)
                                tabSeparator = 0;
                            else if (value[0] != '\0' && value[1] == '\0')
                                tabSeparator = (unsigned char)value[0];
                            else {
                                fprintf(stderr, PROGRAM ": multi-character tab '%s'\n", value);
                                return 2;
                            }
                        } else if (option == 'o') {
                            outputFile = value;
                        }
                        p += strlen(p) - 1;
                        break;
                    }
                    default:
                        fprintf(stderr, PROGRAM ": invalid option -- '%c'\n", *p);
                        try_help();
                        return 2;
                }
            }
        }
    }

    // keys without options of their own use the global ones; with no key the whole line is one
    if (keyCount == 0) {
        Key whole = global;
        whole.startField = whole.startChar = 0;
        whole.hasEnd = 0;
        add_key(&whole);
    } else {
        for (k = 0; k < keyCount; k++) {
            if (!keys[k].hasOptions) {
                Key own = keys[k];
                own.blankStart = own.blankEnd = global.blankStart;
                own.dict = global.dict;
                own.fold = global.fold;
                own.general = global.general;
                own.ignore = global.ignore;
                own.numeric = global.numeric;
                own.reverse = global.reverse;
                keys[k] = own;
            }
        }
    }
    global_reverse = global.reverse;

    text_binary_streams();
    if (operandCount == 0)
        operands[operandCount++] = "-";

    texts = calloc((size_t)operandCount, sizeof *texts);
    lengths = calloc((size_t)operandCount, sizeof *lengths);
    if (texts == NULL || lengths == NULL)
        return 2;

    for (i = 0; i < operandCount; i++) {
        FILE* in;
        size_t offset = 0;
        TextLine line;

        if (text_open(PROGRAM, operands[i], 1, &in) != 0) {
            status = 2;
            continue;
        }
        texts[i] = text_slurp(in, &lengths[i]);
        if (in != stdin)
            fclose(in);
        if (texts[i] == NULL) {
            fprintf(stderr, PROGRAM ": memory exhausted\n");
            return 2;
        }

        while (text_next_line(texts[i], lengths[i], &offset, &line)) {
            if (entryCount >= entryCapacity) {
                size_t capacity = entryCapacity == 0 ? 1024 : entryCapacity * 2;
                Entry* grown = realloc(entries, capacity * sizeof *entries);
                if (grown == NULL) {
                    fprintf(stderr, PROGRAM ": memory exhausted\n");
                    return 2;
                }
                entries = grown;
                entryCapacity = capacity;
            }
            entries[entryCount].line = line;
            entries[entryCount].index = entryCount;
            entryCount++;
        }
    }

    if (status != 0 && check)
        return 2;

    if (check) {
        for (k = 1; k < entryCount; k++) {
            int result = compare_entries(&entries[k - 1], &entries[k]);
            if (result > 0 || (unique && result == 0)) {
                if (!checkQuiet) {
                    fprintf(stderr, PROGRAM ": %s:%lu: disorder: ", operands[0], (unsigned long)(k + 1));
                    fwrite(entries[k].line.content, 1, entries[k].line.length, stderr);
                    fputc('\n', stderr);
                }
                return 1;
            }
        }
        return 0;
    }

    if (entryCount > 1) {
        Entry* scratch = malloc(entryCount * sizeof *scratch);
        if (scratch == NULL) {
            fprintf(stderr, PROGRAM ": memory exhausted\n");
            return 2;
        }
        merge_sort(entries, scratch, entryCount);
        free(scratch);
    }

    if (outputFile != NULL) {
        char native[VPATH_MAX];
        if (vpath_to_native(outputFile, native, sizeof native) != 0 || (out = fopen(native, "wb")) == NULL) {
            fprintf(stderr, PROGRAM ": open failed: %s: No such file or directory\n", outputFile);
            return 2;
        }
    }

    for (k = 0; k < entryCount; k++) {
        if (unique && k > 0 && compare_by_keys(&entries[k - 1].line, &entries[k].line) == 0)
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
    free(operands);
    return status;
}
