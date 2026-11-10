/*
 * fmt: reformats the paragraphs of a text to a width, as GNU fmt does, and with the same algorithm: the cost of every way to
 * break a paragraph into lines (lines near the goal width, sentence ends, short last words and so on) is worked out and the
 * cheapest one is written. Options: -c crown margin, -t tagged paragraphs, -s split only, -u uniform spacing, -p PREFIX,
 * -w WIDTH, -g GOAL and the old -WIDTH. A CR in front of a line feed is part of the line end.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "textutil.h"

#define VERSION "0.1"
#define PROGRAM "fmt"

static void print_usage(void) {
    puts("Usage: fmt [-WIDTH] [OPTION]... [FILE]...\n"
         "Reformat each paragraph in the FILE(s), writing to standard output.\n"
         "The option -WIDTH is an abbreviated form of --width=DIGITS.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  -c, --crown-margin        preserve indentation of first two lines\n"
         "  -p, --prefix=STRING       reformat only lines beginning with STRING,\n"
         "                              reattaching the prefix to reformatted lines\n"
         "  -s, --split-only          split long lines, but do not refill\n"
         "  -t, --tagged-paragraph    indentation of first line different from second\n"
         "  -u, --uniform-spacing     one space between words, two after sentences\n"
         "  -w, --width=WIDTH         maximum line width (default of 75 columns)\n"
         "  -g, --goal=WIDTH          goal width (default of 93% of width)\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit");
}

enum { OPT_CROWN = 1, OPT_PREFIX, OPT_SPLIT, OPT_TAGGED, OPT_UNIFORM, OPT_WIDTH, OPT_GOAL };

static const ArgsOption options[] = {
    { OPT_CROWN,   'c', "crown-margin",     ARGS_NONE },
    { OPT_PREFIX,  'p', "prefix",           ARGS_REQUIRED },
    { OPT_SPLIT,   's', "split-only",       ARGS_NONE },
    { OPT_TAGGED,  't', "tagged-paragraph", ARGS_NONE },
    { OPT_UNIFORM, 'u', "uniform-spacing",  ARGS_NONE },
    { OPT_WIDTH,   'w', "width",            ARGS_REQUIRED },
    { OPT_GOAL,    'g', "goal",             ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

/* ---- the settings ---- */

#define TABWIDTH 8
#define LEEWAY 7
#define MAXCHARS 5000
#define MAXWORDS 1000

typedef long long COST;                 /* 64 bits: a long is only 32 on Windows */

#define MAXCOST LLONG_MAX
#define SQR(n) ((COST)(n) * (COST)(n))
#define EQUIV(n) SQR((COST)(n))
#define SHORT_COST(n) EQUIV((n) * 10)
#define RAGGED_COST(n) (SHORT_COST(n) / 2)
#define LINE_COST EQUIV(70)
#define WIDOW_COST(n) (EQUIV(200) / ((n) + 2))
#define ORPHAN_COST(n) (EQUIV(150) / ((n) + 2))
#define SENTENCE_BONUS EQUIV(50)
#define NOBREAK_COST EQUIV(600)
#define PAREN_BONUS EQUIV(40)
#define PUNCT_BONUS EQUIV(40)

static int crown, tagged, split, uniform;
static const char* prefix = "";
static int prefixFullLength, prefixLength, prefixLeadSpace;
static int maxWidth = 75, goalWidth;

typedef struct Word Word;

struct Word {
    const char* text;
    int         length;
    int         space;
    unsigned    paren : 1;
    unsigned    period : 1;
    unsigned    punct : 1;
    unsigned    final : 1;
    int         lineLength;
    COST        bestCost;
    Word*       nextBreak;
};

static char parabuf[MAXCHARS];
static char* wptr;
static Word word[MAXWORDS];
static Word* wordLimit;

static int inColumn, outColumn, nextPrefixIndent, prefixIndent, firstIndent, otherIndent, lastLineLength;
static int tabs;

/* ---- the input: bytes of a file with CR LF turned into LF ---- */

static const unsigned char* inData;
static size_t inLength, inPos;

static int getch(void) {
    if (inPos >= inLength)
        return EOF;
    if (inData[inPos] == '\r' && inPos + 1 < inLength && inData[inPos + 1] == '\n')
        inPos++;
    return inData[inPos++];
}

static int next_char_value;

/* ---- reading ---- */

static int get_space(int c) {
    for (;;) {
        if (c == ' ') {
            inColumn++;
        } else if (c == '\t') {
            tabs = 1;
            inColumn = (inColumn / TABWIDTH + 1) * TABWIDTH;
        } else {
            return c;
        }
        c = getch();
    }
}

static int get_prefix(void) {
    int c;

    inColumn = 0;
    c = getch();
    c = get_space(c);
    if (prefixLength == 0) {
        nextPrefixIndent = prefixLeadSpace < inColumn ? prefixLeadSpace : inColumn;
    } else {
        const char* p;

        nextPrefixIndent = inColumn;
        for (p = prefix; *p != '\0'; p++) {
            unsigned char pc = (unsigned char)*p;

            if (c != pc)
                return c;
            inColumn++;
            c = getch();
        }
        c = get_space(c);
    }
    return c;
}

static void put_space(int space) {
    int spaceTarget = outColumn + space;

    if (tabs) {
        int tabTarget = spaceTarget / TABWIDTH * TABWIDTH;

        if (outColumn + 1 < tabTarget) {
            while (outColumn < tabTarget) {
                putchar('\t');
                outColumn = (outColumn / TABWIDTH + 1) * TABWIDTH;
            }
        }
    }
    while (outColumn < spaceTarget) {
        putchar(' ');
        outColumn++;
    }
}

static int copy_rest(int c) {
    const char* s;

    outColumn = 0;
    if (inColumn > nextPrefixIndent || (c != '\n' && c != EOF)) {
        put_space(nextPrefixIndent);
        for (s = prefix; outColumn != inColumn && *s != '\0'; outColumn++)
            putchar(*s++);
        if (c != EOF && c != '\n')
            put_space(inColumn - outColumn);
        if (c == EOF && inColumn >= nextPrefixIndent + prefixLength)
            putchar('\n');
    }
    while (c != '\n' && c != EOF) {
        putchar(c);
        c = getch();
    }
    return c;
}

static int is_open(int c) {
    return c != '\0' && strchr("(['`\"", c) != NULL;
}

static int is_close(int c) {
    return c != '\0' && strchr(")]'\"", c) != NULL;
}

static int is_period(int c) {
    return c != '\0' && strchr(".?!", c) != NULL;
}

static void check_punctuation(Word* w) {
    const char* start = w->text;
    const char* finish = start + (w->length - 1);
    unsigned char fin = (unsigned char)*finish;

    w->paren = is_open((unsigned char)*start) ? 1 : 0;
    w->punct = ispunct(fin) ? 1 : 0;
    while (start < finish && is_close((unsigned char)*finish))
        finish--;
    w->period = is_period((unsigned char)*finish) ? 1 : 0;
}

/* ---- the costs ---- */

static COST base_cost(Word* w) {
    COST cost = LINE_COST;

    if (w > word) {
        if ((w - 1)->period) {
            if ((w - 1)->final)
                cost -= SENTENCE_BONUS;
            else
                cost += NOBREAK_COST;
        } else if ((w - 1)->punct) {
            cost -= PUNCT_BONUS;
        } else if (w > word + 1 && (w - 2)->final) {
            cost += WIDOW_COST((w - 1)->length);
        }
    }
    if (w->paren)
        cost -= PAREN_BONUS;
    else if (w->final)
        cost += ORPHAN_COST(w->length);
    return cost;
}

static COST line_cost(Word* next, int len) {
    int n;
    COST cost;

    if (next == wordLimit)
        return 0;
    n = goalWidth - len;
    cost = SHORT_COST(n);
    if (next->nextBreak != wordLimit) {
        n = len - next->lineLength;
        cost += RAGGED_COST(n);
    }
    return cost;
}

static void fmt_paragraph(void) {
    Word* start;
    Word* w;
    int len;
    COST wcost, best;
    int savedLength;

    wordLimit->bestCost = 0;
    savedLength = wordLimit->length;
    wordLimit->length = maxWidth;
    for (start = wordLimit - 1; start >= word; start--) {
        best = MAXCOST;
        len = start == word ? firstIndent : otherIndent;
        w = start;
        len += w->length;
        do {
            w++;
            wcost = line_cost(w, len) + w->bestCost;
            if (start == word && lastLineLength > 0)
                wcost += RAGGED_COST(len - lastLineLength);
            if (wcost < best) {
                best = wcost;
                start->nextBreak = w;
                start->lineLength = len;
            }
            if (w == wordLimit)
                break;
            len += (w - 1)->space + w->length;
        } while (len < maxWidth);
        start->bestCost = best + base_cost(start);
    }
    wordLimit->length = savedLength;
}

/* ---- writing ---- */

static void put_word(Word* w) {
    const char* s = w->text;
    int n;

    for (n = w->length; n > 0; n--)
        putchar((unsigned char)*s++);
    outColumn += w->length;
}

static void put_line(Word* w, int indent) {
    Word* endline;

    outColumn = 0;
    put_space(prefixIndent);
    fputs(prefix, stdout);
    outColumn += prefixLength;
    put_space(indent - outColumn);
    endline = w->nextBreak - 1;
    for (; w != endline; w++) {
        put_word(w);
        put_space(w->space);
    }
    put_word(w);
    lastLineLength = outColumn;
    putchar('\n');
}

static void put_paragraph(Word* finish) {
    Word* w;

    put_line(word, firstIndent);
    for (w = word->nextBreak; w != finish; w = w->nextBreak)
        put_line(w, otherIndent);
}

static void set_other_indent(int sameParagraph) {
    if (split) {
        otherIndent = firstIndent;
    } else if (crown) {
        otherIndent = sameParagraph ? inColumn : firstIndent;
    } else if (tagged) {
        if (sameParagraph && inColumn != firstIndent) {
            otherIndent = inColumn;
        } else if (otherIndent == firstIndent) {
            otherIndent = firstIndent == 0 ? 3 : 0;
        }
    } else {
        otherIndent = firstIndent;
    }
}

static void flush_paragraph(void) {
    Word* splitPoint;
    Word* w;
    COST bestBreak;
    ptrdiff_t shift;
    size_t i, count;

    if (wordLimit == word) {
        fwrite(parabuf, 1, (size_t)(wptr - parabuf), stdout);
        wptr = parabuf;
        wordLimit->text = wptr;                    // the word being read starts again
        return;
    }
    fmt_paragraph();
    splitPoint = wordLimit;
    bestBreak = MAXCOST;
    for (w = word->nextBreak; w != wordLimit; w = w->nextBreak) {
        if (w->bestCost - w->nextBreak->bestCost < bestBreak) {
            splitPoint = w;
            bestBreak = w->bestCost - w->nextBreak->bestCost;
        }
        if (bestBreak <= MAXCOST - LINE_COST)
            bestBreak += LINE_COST;
    }
    put_paragraph(splitPoint);
    shift = splitPoint->text - parabuf;
    memmove(parabuf, splitPoint->text, (size_t)(wptr - splitPoint->text));
    wptr -= shift;
    count = (size_t)(wordLimit - splitPoint) + 1;
    for (i = 0; i < count; i++) {
        word[i] = splitPoint[i];
        word[i].text -= shift;
    }
    wordLimit -= splitPoint - word;
}

static int get_line(int c) {
    int start;
    char* endOfParabuf = &parabuf[MAXCHARS];
    Word* endOfWord = &word[MAXWORDS - 2];

    do {
        wordLimit->text = wptr;
        do {
            if (wptr == endOfParabuf) {
                set_other_indent(1);
                flush_paragraph();
            }
            *wptr++ = (char)c;
            c = getch();
        } while (c != EOF && !isspace(c));
        wordLimit->length = (int)(wptr - wordLimit->text);
        inColumn += wordLimit->length;
        check_punctuation(wordLimit);
        start = inColumn;
        c = get_space(c);
        wordLimit->space = inColumn - start;
        wordLimit->final = (c == EOF || (wordLimit->period && (c == '\n' || wordLimit->space > 1))) ? 1 : 0;
        if (c == '\n' || c == EOF || uniform)
            wordLimit->space = wordLimit->final ? 2 : 1;
        if (wordLimit == endOfWord) {
            set_other_indent(1);
            flush_paragraph();
        }
        wordLimit++;
    } while (c != '\n' && c != EOF);
    return get_prefix();
}

static int same_para(int c) {
    return nextPrefixIndent == prefixIndent && inColumn >= nextPrefixIndent + prefixFullLength && c != '\n' && c != EOF;
}

static int get_paragraph(void) {
    int c;

    lastLineLength = 0;
    c = next_char_value;
    while (c == '\n' || c == EOF || nextPrefixIndent < prefixLeadSpace || inColumn < nextPrefixIndent + prefixFullLength) {
        c = copy_rest(c);
        if (c == EOF) {
            next_char_value = EOF;
            return 0;
        }
        putchar('\n');
        c = get_prefix();
    }
    prefixIndent = nextPrefixIndent;
    firstIndent = inColumn;
    wptr = parabuf;
    wordLimit = word;
    c = get_line(c);
    set_other_indent(same_para(c));
    if (split) {
        // each line stands for itself
    } else if (tagged && same_para(c) && inColumn == firstIndent) {
        // the second line has the indent of the first: the first line is a paragraph by itself
    } else {
        while (same_para(c) && inColumn == otherIndent)
            c = get_line(c);
    }
    (wordLimit - 1)->final = 1;                    // the end of the paragraph ends a sentence
    next_char_value = c;
    return 1;
}

static void fmt_file(void) {
    tabs = 0;
    otherIndent = 0;
    next_char_value = get_prefix();
    while (get_paragraph()) {
        fmt_paragraph();
        put_paragraph(wordLimit);
    }
}

/* ---- the options ---- */

static const char* widthOption;
static const char* goalOption;

static void set_prefix(const char* p) {
    char* copy = _strdup(p);
    char* s;
    char* q = copy;

    prefixLeadSpace = 0;
    while (*q == ' ') {
        prefixLeadSpace++;
        q++;
    }
    prefix = q;
    prefixFullLength = (int)strlen(q);
    s = q + prefixFullLength;
    while (s > q && s[-1] == ' ')
        s--;
    *s = '\0';
    prefixLength = (int)(s - q);
}

static int on_option(int id, const char* value, void* context) {
    (void)context;
    switch (id) {
    case OPT_CROWN: crown = 1; break;
    case OPT_PREFIX: set_prefix(value); break;
    case OPT_SPLIT: split = 1; break;
    case OPT_TAGGED: tagged = 1; break;
    case OPT_UNIFORM: uniform = 1; break;
    case OPT_WIDTH: widthOption = value; break;
    case OPT_GOAL: goalOption = value; break;
    default: break;
    }
    return 0;
}

static const char* quote(const char* text) {
    static char buffer[1024];
    size_t n = 0;
    const unsigned char* p;

    buffer[n++] = '\'';
    for (p = (const unsigned char*)text; *p != '\0' && n < sizeof buffer - 8; p++) {
        if (*p == '\\' || *p == '\'')
            buffer[n++] = '\\';
        buffer[n++] = (char)*p;
    }
    buffer[n++] = '\'';
    buffer[n] = '\0';
    return buffer;
}

/** A width as xdectoumax reads it: digits only, at most `limit`. Returns 0, or -1 after the message. */
static int parse_width(const char* text, unsigned long limit, unsigned long* out) {
    const char* p = text;
    unsigned long long value = 0;
    int overflow = 0;

    if (!isdigit((unsigned char)*p)) {
        fprintf(stderr, PROGRAM ": invalid width: %s\n", quote(text));
        return -1;
    }
    for (; isdigit((unsigned char)*p); p++) {
        value = value * 10 + (unsigned long long)(*p - '0');
        if (value > 0xFFFFFFFFFFULL)
            overflow = 1, value = 0xFFFFFFFFFFULL;
    }
    if (*p != '\0') {
        fprintf(stderr, PROGRAM ": invalid width: %s\n", quote(text));
        return -1;
    }
    if (overflow || value >= (1ULL << 30)) {
        fprintf(stderr, PROGRAM ": invalid width: %s: Value too large for defined data type\n", quote(text));
        return -1;
    }
    if (value > limit) {
        fprintf(stderr, PROGRAM ": invalid width: %s: Numerical result out of range\n", quote(text));
        return -1;
    }
    *out = (unsigned long)value;
    return 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, i, ok = 1;
    char** args;
    int newArgc = 0;
    unsigned long v;
    char legacy[32];
    int haveWidth, haveGoal;

    _setmode(_fileno(stdout), _O_BINARY);
    // the old form -WIDTH, only as the first argument
    args = malloc(((size_t)argc + 2) * sizeof *args);
    for (i = 0; i < argc; i++) {
        if (i == 1 && argv[1][0] == '-' && isdigit((unsigned char)argv[1][1])) {
            snprintf(legacy, sizeof legacy, "%s", argv[1] + 1);
            widthOption = legacy;
            continue;
        }
        args[newArgc++] = argv[i];
    }
    args[newArgc] = NULL;
    result = args_parse(&spec, newArgc, args, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    haveWidth = widthOption != NULL;
    haveGoal = goalOption != NULL;
    if (haveWidth) {
        if (parse_width(widthOption, MAXCHARS / 2, &v) != 0)
            return 1;
        maxWidth = (int)v;
    }
    if (haveGoal) {
        if (parse_width(goalOption, (unsigned long)maxWidth, &v) != 0)
            return 1;
        goalWidth = (int)v;
        if (!haveWidth)
            maxWidth = goalWidth + 10;
    } else {
        goalWidth = maxWidth * (2 * (100 - LEEWAY) + 1) / 200;
    }
    if (count == 0) {
        static const char* dash[2] = { "-", NULL };

        operands = dash;
        count = 1;
    }
    for (i = 0; i < count; i++) {
        FILE* in;
        unsigned char* text;
        size_t length;

        if (text_open(PROGRAM, operands[i], 0, &in) != 0) {
            ok = 0;
            continue;
        }
        text = text_slurp(in, &length);
        if (in != stdin)
            fclose(in);
        if (text == NULL) {
            static unsigned char empty[1];

            text = empty;
            length = 0;
        }
        inData = text;
        inLength = length;
        inPos = 0;
        fmt_file();
        if (text != NULL && length > 0)
            free(text);
    }
    return ok ? 0 : 1;
}
