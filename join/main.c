/*
 * join: joins the lines of two sorted files on a common field, as GNU join does: fields split at blanks (or at the
 * character of -t), the lines of equal keys are paired every one with every one, the others are written with -a / -v,
 * -o chooses the output fields, -e fills the missing ones. It follows the merge of GNU join step by step, also in what
 * it checks of the order (a line out of order is reported with its file and line number, and the status is 1).
 * A line ending in CR LF is a line; the line written ends as the line of the second file (or the first) did.
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "textutil.h"

#define VERSION "0.1"
#define PROGRAM "join"

static void print_usage(void) {
    puts("Usage: join [OPTION]... FILE1 FILE2\n"
         "For each pair of input lines with identical join fields, write a line to\n"
         "standard output.  The default join field is the first, delimited by blanks.\n"
         "\n"
         "When FILE1 or FILE2 (not both) is -, read standard input.\n"
         "\n"
         "  -a FILENUM        also print unpairable lines from file FILENUM, where\n"
         "                      FILENUM is 1 or 2, corresponding to FILE1 or FILE2\n"
         "  -e STRING         replace missing input fields with STRING\n"
         "  -i, --ignore-case  ignore differences in case when comparing fields\n"
         "  -j FIELD          equivalent to '-1 FIELD -2 FIELD'\n"
         "  -o FORMAT         obey FORMAT while constructing output line\n"
         "  -t CHAR           use CHAR as input and output field separator\n"
         "  -v FILENUM        like -a FILENUM, but suppress joined output lines\n"
         "  -1 FIELD          join on this FIELD of file 1\n"
         "  -2 FIELD          join on this FIELD of file 2\n"
         "      --check-order  check that the input is correctly sorted, even if all input lines are pairable\n"
         "      --nocheck-order  do not check that the input is correctly sorted\n"
         "      --header      treat the first line in each file as field headers,\n"
         "                      print them without trying to pair them\n"
         "  -z, --zero-terminated     line delimiter is NUL, not newline\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Unless -t CHAR is given, leading blanks separate fields and are ignored,\n"
         "else fields are separated by CHAR.  Any FIELD is a field number counted from 1.\n"
         "FORMAT is one or more comma or blank separated specifications, each being\n"
         "'FILENUM.FIELD' or '0'.  Default FORMAT outputs the join field, the remaining\n"
         "fields from FILE1, the remaining fields from FILE2, all separated by CHAR.\n"
         "If FORMAT is the keyword 'auto', then the first line of each file determines\n"
         "the number of fields output for each line.\n"
         "\n"
         "Important: FILE1 and FILE2 must be sorted on the join fields.");
}

enum { OPT_A = 1, OPT_E, OPT_I, OPT_J, OPT_O, OPT_T, OPT_V, OPT_1, OPT_2, OPT_CHECK, OPT_NOCHECK, OPT_HEADER, OPT_ZERO };

static const ArgsOption options[] = {
    { OPT_A,       'a', NULL,             ARGS_REQUIRED },
    { OPT_E,       'e', NULL,             ARGS_REQUIRED },
    { OPT_I,       'i', "ignore-case",    ARGS_NONE },
    { OPT_J,       'j', NULL,             ARGS_REQUIRED },
    { OPT_O,       'o', NULL,             ARGS_REQUIRED },
    { OPT_T,       't', NULL,             ARGS_REQUIRED },
    { OPT_V,       'v', NULL,             ARGS_REQUIRED },
    { OPT_1,       '1', NULL,             ARGS_REQUIRED },
    { OPT_2,       '2', NULL,             ARGS_REQUIRED },
    { OPT_CHECK,   0,   "check-order",    ARGS_NONE },
    { OPT_NOCHECK, 0,   "nocheck-order",  ARGS_NONE },
    { OPT_HEADER,  0,   "header",         ARGS_NONE },
    { OPT_ZERO,    'z', "zero-terminated", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct { const unsigned char* beg; size_t len; } Field;

typedef struct {
    const unsigned char* text;
    size_t               length;
    int                  ending;        // 0 none (the last line), 1 delimiter, 2 CR LF
    Field*               fields;
    size_t               count;
} Line;

typedef struct { int file; size_t field; } OutField;    // file 0 is the join field

enum { CHECK_DEFAULT, CHECK_ENABLED, CHECK_DISABLED };

typedef struct {
    int         printUnpairable[2];
    int         printPairable;
    const char* emptyFiller;
    int         ignoreCase;
    size_t      joinField[2];           // counted from 0
    int         tab;                    // -1 blanks, else the character ('\n' for the whole line)
    int         check;
    int         header;
    int         autoformat;
    int         terminator;
    OutField*   outlist;
    size_t      outCount, outCapacity;
    size_t      autocount[2];
} Config;

static Config cfg;

/** The text as GNU quote() writes it: in single quotes, a backslash, a quote and unprintable bytes escaped. */
static const char* quote(const char* text) {
    static char buffer[1024];
    size_t n = 0;
    const unsigned char* p;

    buffer[n++] = '\'';
    for (p = (const unsigned char*)text; *p != '\0' && n < sizeof buffer - 8; p++) {
        switch (*p) {
        case '\\': buffer[n++] = '\\'; buffer[n++] = '\\'; break;
        case '\'': buffer[n++] = '\\'; buffer[n++] = '\''; break;
        case '\n': buffer[n++] = '\\'; buffer[n++] = 'n'; break;
        case '\t': buffer[n++] = '\\'; buffer[n++] = 't'; break;
        case '\r': buffer[n++] = '\\'; buffer[n++] = 'r'; break;
        default:
            if (*p < 32 || *p == 127)
                n += (size_t)snprintf(buffer + n, 8, "\\%03o", *p);
            else
                buffer[n++] = (char)*p;
        }
    }
    buffer[n++] = '\'';
    buffer[n] = '\0';
    return buffer;
}

static void die(const char* format, const char* argument) {
    fflush(stdout);
    fprintf(stderr, PROGRAM ": ");
    fprintf(stderr, format, argument);
    fputc('\n', stderr);
    exit(1);
}

/** A field number as xstrtoul reads it (blanks first, + allowed, not 0, an overflow is a huge number). Counted from 0. */
static int field_number(const char* text, size_t* out) {
    const char* p = text;
    unsigned long long value = 0;
    int overflow = 0;

    while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
        p++;
    if (*p == '+')
        p++;
    if (*p < '0' || *p > '9')
        return -1;
    for (; *p >= '0' && *p <= '9'; p++) {
        if (value > (ULLONG_MAX - (unsigned)(*p - '0')) / 10)
            overflow = 1;
        else
            value = value * 10 + (unsigned)(*p - '0');
    }
    if (*p != '\0')
        return -1;
    if (overflow)
        value = ULLONG_MAX;
    if (value == 0)
        return -1;
    *out = (size_t)(value - 1);
    return 0;
}

static size_t join_field_of(const char* text) {
    size_t n;

    if (field_number(text, &n) != 0)
        die("invalid field number: %s", quote(text));
    return n;
}

static void add_outfield(int file, size_t field) {
    if (cfg.outCount == cfg.outCapacity) {
        cfg.outCapacity = cfg.outCapacity == 0 ? 16 : cfg.outCapacity * 2;
        cfg.outlist = realloc(cfg.outlist, cfg.outCapacity * sizeof *cfg.outlist);
        if (cfg.outlist == NULL)
            die("memory exhausted%s", "");
    }
    cfg.outlist[cfg.outCount].file = file;
    cfg.outlist[cfg.outCount].field = field;
    cfg.outCount++;
}

static void decode_field_spec(const char* spec_text) {
    switch (spec_text[0]) {
    case '0':
        if (spec_text[1] != '\0')
            die("invalid field specifier: %s", quote(spec_text));
        add_outfield(0, 0);
        break;
    case '1':
    case '2':
        if (spec_text[1] != '.')
            die("invalid field specifier: %s", quote(spec_text));
        add_outfield(spec_text[0] - '0', join_field_of(spec_text + 2));
        break;
    default:
        die("invalid file number in field spec: %s", quote(spec_text));
    }
}

static void add_field_list(const char* list) {
    const char* p = list;

    for (;;) {
        size_t n = strcspn(p, ", \t");
        char token[256];

        if (n >= sizeof token)
            n = sizeof token - 1;
        memcpy(token, p, n);
        token[n] = '\0';
        decode_field_spec(token);
        p += strcspn(p, ", \t");
        if (*p == '\0')
            break;
        p++;
    }
}

static int on_option(int id, const char* value, void* context) {
    (void)context;
    switch (id) {
    case OPT_A:
    case OPT_V: {
        size_t n;

        if (field_number(value, &n) != 0 || n > 1)
            die("invalid field number: %s", quote(value));
        cfg.printUnpairable[n] = 1;
        if (id == OPT_V)
            cfg.printPairable = 0;
        break;
    }
    case OPT_E:
        if (cfg.emptyFiller != NULL && strcmp(cfg.emptyFiller, value) != 0)
            die("conflicting empty-field replacement strings%s", "");
        cfg.emptyFiller = value;
        break;
    case OPT_I: cfg.ignoreCase = 1; break;
    case OPT_J: cfg.joinField[0] = cfg.joinField[1] = join_field_of(value); break;
    case OPT_1: cfg.joinField[0] = join_field_of(value); break;
    case OPT_2: cfg.joinField[1] = join_field_of(value); break;
    case OPT_O:
        if (strcmp(value, "auto") == 0) {
            cfg.autoformat = 1;
        } else {
            cfg.autoformat = 0;
            add_field_list(value);
        }
        break;
    case OPT_T: {
        int tab = (unsigned char)value[0];

        if (tab == 0) {
            tab = '\n';
        } else if (value[1] != '\0') {
            if (strcmp(value, "\\0") == 0)
                tab = 0;
            else
                die("multi-character tab %s", quote(value));
        }
        if (cfg.tab >= 0 && cfg.tab != tab)
            die("incompatible tabs%s", "");
        cfg.tab = tab;
        break;
    }
    case OPT_CHECK: cfg.check = CHECK_ENABLED; break;
    case OPT_NOCHECK: cfg.check = CHECK_DISABLED; break;
    case OPT_HEADER: cfg.header = 1; break;
    case OPT_ZERO: cfg.terminator = 0; break;
    default: break;
    }
    return 0;
}

/** One input file, read completely, line by line as GNU join gets them (and checked for order as they are read). */
typedef struct {
    const char*    name;
    unsigned char* text;
    size_t         length, pos;
    unsigned long  lineNumber;
    int            which;               // 0 or 1
    unsigned char* previousKey;
    size_t         previousLength;
    int            hasPrevious;
    int            warned;
} Input;

static int seen_unpairable;

static int key_of(const Line* line, size_t joinField, const unsigned char** beg, size_t* len) {
    if (joinField < line->count) {
        *beg = line->fields[joinField].beg;
        *len = line->fields[joinField].len;
        return 1;
    }
    *beg = (const unsigned char*)"";
    *len = 0;
    return 0;
}

static int compare_keys(const unsigned char* a, size_t alen, const unsigned char* b, size_t blen) {
    size_t n, i;

    if (alen == 0)
        return blen == 0 ? 0 : -1;
    if (blen == 0)
        return 1;
    n = alen < blen ? alen : blen;
    if (cfg.ignoreCase) {
        for (i = 0; i < n; i++) {
            int x = toupper(a[i]), y = toupper(b[i]);

            if (x != y)
                return x < y ? -1 : 1;
        }
    } else {
        int d = memcmp(a, b, n);

        if (d != 0)
            return d < 0 ? -1 : 1;
    }
    return alen < blen ? -1 : (alen != blen ? 1 : 0);
}

static int keycmp(const Line* l1, const Line* l2) {
    const unsigned char *a, *b;
    size_t alen, blen;

    key_of(l1, cfg.joinField[0], &a, &alen);
    key_of(l2, cfg.joinField[1], &b, &blen);
    return compare_keys(a, alen, b, blen);
}

static void extract_field(Line* line, const unsigned char* beg, size_t len) {
    if ((line->count & (line->count - 1)) == 0) {          // grows at 0, 1, 2, 4, 8 ...
        size_t capacity = line->count == 0 ? 1 : line->count * 2;

        line->fields = realloc(line->fields, capacity * sizeof *line->fields);
        if (line->fields == NULL)
            die("memory exhausted%s", "");
    }
    line->fields[line->count].beg = beg;
    line->fields[line->count].len = len;
    line->count++;
}

static int field_sep(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n';
}

static void split_fields(Line* line) {
    const unsigned char* ptr = line->text;
    const unsigned char* lim = ptr + line->length;

    if (ptr == lim)
        return;
    if (cfg.tab >= 0 && cfg.tab != '\n') {
        const unsigned char* sep;

        while ((sep = memchr(ptr, cfg.tab, (size_t)(lim - ptr))) != NULL) {
            extract_field(line, ptr, (size_t)(sep - ptr));
            ptr = sep + 1;
        }
    } else if (cfg.tab < 0) {
        while (field_sep(*ptr))
            if (++ptr == lim)
                return;
        do {
            const unsigned char* sep;

            for (sep = ptr + 1; sep != lim && !field_sep(*sep); sep++)
                ;
            extract_field(line, ptr, (size_t)(sep - ptr));
            if (sep == lim)
                return;
            for (ptr = sep + 1; ptr != lim && field_sep(*ptr); ptr++)
                ;
        } while (ptr != lim);
    }
    extract_field(line, ptr, (size_t)(lim - ptr));
}

static void line_free(Line* line) {
    if (line != NULL) {
        free(line->fields);
        free(line);
    }
}

static void check_order(Input* in, const Line* line) {
    const unsigned char *beg;
    size_t len;

    key_of(line, cfg.joinField[in->which], &beg, &len);
    if (in->hasPrevious && cfg.check != CHECK_DISABLED && (cfg.check == CHECK_ENABLED || seen_unpairable) &&
        !in->warned && compare_keys(in->previousKey, in->previousLength, beg, len) > 0) {
        fflush(stdout);
        fprintf(stderr, PROGRAM ": %s:%lu: is not sorted: %.*s\n", in->name, in->lineNumber, (int)line->length,
                (const char*)line->text);
        in->warned = 1;
        if (cfg.check == CHECK_ENABLED)
            exit(1);
    }
    in->previousKey = realloc(in->previousKey, len + 1);
    if (in->previousKey == NULL)
        die("memory exhausted%s", "");
    memcpy(in->previousKey, beg, len);
    in->previousLength = len;
    in->hasPrevious = 1;
}

/** The next line of the file, or NULL at its end. */
static Line* get_line(Input* in) {
    Line* line;
    const unsigned char* end;
    size_t stop;

    if (in->pos >= in->length)
        return NULL;
    line = calloc(1, sizeof *line);
    if (line == NULL)
        die("memory exhausted%s", "");
    end = memchr(in->text + in->pos, cfg.terminator, in->length - in->pos);
    stop = end != NULL ? (size_t)(end - in->text) : in->length;
    line->text = in->text + in->pos;
    line->length = stop - in->pos;
    line->ending = end != NULL ? 1 : 0;
    if (cfg.terminator == '\n' && end != NULL && line->length > 0 && line->text[line->length - 1] == '\r') {
        line->length--;
        line->ending = 2;
    }
    in->pos = end != NULL ? stop + 1 : stop;
    in->lineNumber++;
    split_fields(line);
    check_order(in, line);
    return line;
}

/** The lines with one key, the lines of a file that are being gathered. */
typedef struct {
    Line** lines;
    size_t count, capacity;
} Seq;

static int get_seq(Input* in, Seq* seq) {
    Line* line = get_line(in);

    if (line == NULL)
        return 0;
    if (seq->count == seq->capacity) {
        seq->capacity = seq->capacity == 0 ? 8 : seq->capacity * 2;
        seq->lines = realloc(seq->lines, seq->capacity * sizeof *seq->lines);
        if (seq->lines == NULL)
            die("memory exhausted%s", "");
    }
    seq->lines[seq->count++] = line;
    return 1;
}

static int advance_seq(Input* in, Seq* seq, int first) {
    if (first) {
        size_t i;

        for (i = 0; i < seq->count; i++)
            line_free(seq->lines[i]);
        seq->count = 0;
    }
    return get_seq(in, seq);
}

static void put_field(size_t n, const Line* line) {
    if (n < line->count) {
        if (line->fields[n].len > 0)
            fwrite(line->fields[n].beg, 1, line->fields[n].len, stdout);
        else if (cfg.emptyFiller != NULL)
            fputs(cfg.emptyFiller, stdout);
    } else if (cfg.emptyFiller != NULL) {
        fputs(cfg.emptyFiller, stdout);
    }
}

static void put_fields(const Line* line, size_t joinField, size_t autocount) {
    size_t i, count = cfg.autoformat ? autocount : line->count;
    char sep = cfg.tab < 0 ? ' ' : (char)cfg.tab;

    for (i = 0; i < joinField && i < count; i++) {
        putchar(sep);
        put_field(i, line);
    }
    for (i = joinField + 1; i < count; i++) {
        putchar(sep);
        put_field(i, line);
    }
}

static void put_join(const Line* line1, const Line* line2) {
    char sep = cfg.tab < 0 ? ' ' : (char)cfg.tab;
    const Line* last = line2 != NULL ? line2 : line1;

    if (cfg.outCount > 0) {
        size_t i;

        for (i = 0; i < cfg.outCount; i++) {
            const Line* line;
            size_t field;

            if (cfg.outlist[i].file == 0) {
                if (line1 != NULL) {
                    field = cfg.joinField[0];
                    line = line1;
                } else {
                    field = cfg.joinField[1];
                    line = line2;
                }
            } else {
                line = cfg.outlist[i].file == 1 ? line1 : line2;
                field = cfg.outlist[i].field;
            }
            if (line == NULL || field >= line->count) {
                if (cfg.emptyFiller != NULL)
                    fputs(cfg.emptyFiller, stdout);
            } else {
                put_field(field, line);
            }
            if (i + 1 < cfg.outCount)
                putchar(sep);
        }
    } else {
        if (line1 != NULL) {
            put_field(cfg.joinField[0], line1);
            put_fields(line1, cfg.joinField[0], cfg.autocount[0]);
        } else {
            put_field(cfg.joinField[1], line2);
        }
        if (line1 != NULL && line2 != NULL)
            put_fields(line2, cfg.joinField[1], cfg.autocount[1]);
        else if (line1 == NULL)
            put_fields(line2, cfg.joinField[1], cfg.autocount[1]);
    }
    if (cfg.terminator == '\n' && last->ending == 2)
        putchar('\r');
    putchar(cfg.terminator);
}

static void swap_lines(Line** a, Line** b) {
    Line* t = *a;

    *a = *b;
    *b = t;
}

static void join_files(Input* in1, Input* in2) {
    Seq seq1, seq2;
    int diff, eof1, eof2, checktail;

    memset(&seq1, 0, sizeof seq1);
    memset(&seq2, 0, sizeof seq2);
    advance_seq(in1, &seq1, 1);
    advance_seq(in2, &seq2, 1);
    if (cfg.autoformat) {
        cfg.autocount[0] = seq1.count ? seq1.lines[0]->count : 0;
        cfg.autocount[1] = seq2.count ? seq2.lines[0]->count : 0;
        if (cfg.outCount == 0) {            // the format that follows the first lines
            size_t i;

            add_outfield(0, 0);
            for (i = 0; i < cfg.autocount[0]; i++)
                if (i != cfg.joinField[0])
                    add_outfield(1, i);
            for (i = 0; i < cfg.autocount[1]; i++)
                if (i != cfg.joinField[1])
                    add_outfield(2, i);
        }
    }
    if (cfg.header && (seq1.count || seq2.count)) {
        put_join(seq1.count ? seq1.lines[0] : NULL, seq2.count ? seq2.lines[0] : NULL);
        if (seq1.count)
            advance_seq(in1, &seq1, 1);
        if (seq2.count)
            advance_seq(in2, &seq2, 1);
    }

    while (seq1.count && seq2.count) {
        size_t i;

        diff = keycmp(seq1.lines[0], seq2.lines[0]);
        if (diff < 0) {
            if (cfg.printUnpairable[0])
                put_join(seq1.lines[0], NULL);
            advance_seq(in1, &seq1, 1);
            seen_unpairable = 1;
            continue;
        }
        if (diff > 0) {
            if (cfg.printUnpairable[1])
                put_join(NULL, seq2.lines[0]);
            advance_seq(in2, &seq2, 1);
            seen_unpairable = 1;
            continue;
        }

        eof1 = 0;
        do {
            if (!advance_seq(in1, &seq1, 0)) {
                eof1 = 1;
                seq1.count++;
                break;
            }
        } while (!keycmp(seq1.lines[seq1.count - 1], seq2.lines[0]));
        eof2 = 0;
        do {
            if (!advance_seq(in2, &seq2, 0)) {
                eof2 = 1;
                seq2.count++;
                break;
            }
        } while (!keycmp(seq1.lines[0], seq2.lines[seq2.count - 1]));

        if (cfg.printPairable) {
            for (i = 0; i + 1 < seq1.count; i++) {
                size_t j;

                for (j = 0; j + 1 < seq2.count; j++)
                    put_join(seq1.lines[i], seq2.lines[j]);
            }
        }
        // what is read ahead (the first line with another key) becomes the first of the next group
        if (!eof1) {
            swap_lines(&seq1.lines[0], &seq1.lines[seq1.count - 1]);
            for (i = 1; i < seq1.count; i++)
                line_free(seq1.lines[i]);
            seq1.count = 1;
        } else {
            for (i = 0; i + 1 < seq1.count; i++)
                line_free(seq1.lines[i]);
            seq1.count = 0;
        }
        if (!eof2) {
            swap_lines(&seq2.lines[0], &seq2.lines[seq2.count - 1]);
            for (i = 1; i < seq2.count; i++)
                line_free(seq2.lines[i]);
            seq2.count = 1;
        } else {
            for (i = 0; i + 1 < seq2.count; i++)
                line_free(seq2.lines[i]);
            seq2.count = 0;
        }
    }

    // the tails are read to check their order (unless the warning was given for both) and to write the unpairable lines
    checktail = cfg.check != CHECK_DISABLED && !(in1->warned && in2->warned);
    if ((cfg.printUnpairable[0] || checktail) && seq1.count) {
        if (cfg.printUnpairable[0])
            put_join(seq1.lines[0], NULL);
        if (seq2.count)
            seen_unpairable = 1;
        while (advance_seq(in1, &seq1, 1)) {
            if (cfg.printUnpairable[0])
                put_join(seq1.lines[0], NULL);
        }
    }
    if ((cfg.printUnpairable[1] || checktail) && seq2.count) {
        if (cfg.printUnpairable[1])
            put_join(NULL, seq2.lines[0]);
        if (seq1.count)
            seen_unpairable = 1;
        while (advance_seq(in2, &seq2, 1)) {
            if (cfg.printUnpairable[1])
                put_join(NULL, seq2.lines[0]);
        }
    }
}

static int open_input(Input* in, const char* name, int which) {
    FILE* file;

    memset(in, 0, sizeof *in);
    in->name = name;
    in->which = which;
    if (text_open(PROGRAM, name, 1, &file) != 0)
        return -1;
    in->text = text_slurp(file, &in->length);
    if (file != stdin)
        fclose(file);
    if (in->text == NULL) {
        fprintf(stderr, PROGRAM ": read error: %s\n", file == stdin ? "Input/output error" : "Is a directory");
        return -1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result;
    Input in1, in2;

    memset(&cfg, 0, sizeof cfg);
    cfg.printPairable = 1;
    cfg.tab = -1;
    cfg.terminator = '\n';
    text_binary_streams();
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count != 2) {
        if (count == 0)
            fprintf(stderr, PROGRAM ": missing operand\n");
        else if (count == 1)
            fprintf(stderr, PROGRAM ": missing operand after '%s'\n", operands[0]);
        else
            fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[2]);
        args_usage_error(&spec);
        return 1;
    }
    if (strcmp(operands[0], "-") == 0 && strcmp(operands[1], "-") == 0) {
        fprintf(stderr, PROGRAM ": both files cannot be standard input\n");
        return 1;
    }
    if (open_input(&in1, operands[0], 0) != 0 || open_input(&in2, operands[1], 1) != 0)
        return 1;

    join_files(&in1, &in2);
    if (fflush(stdout) != 0) {
        fprintf(stderr, PROGRAM ": write error\n");
        return 1;
    }
    if (in1.warned || in2.warned) {
        fprintf(stderr, PROGRAM ": input is not in sorted order\n");
        return 1;
    }
    return 0;
}
