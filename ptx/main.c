/*
 * ptx: a permuted index of the words in the input, as GNU ptx does it. Every word is a keyword; a line of the index shows
 * the keyword with the text round it: the text after it, the text before it, and what does not fit in either goes to the
 * other side (the "tail" in front of the left field and the "head" after the right one), with a mark (-F, "/") where the
 * text was cut. The output can be plain text for a terminal, roff (-O) or TeX (-T) directives.
 * Options: -A references made of file and line, -r references in the input, -R references on the right, -f fold case,
 * -i/-o words to ignore/to keep, -b word break characters, -W word regexp, -S sentence regexp, -g gap, -w width,
 * -F truncation mark, -M macro name, -G the old (System V) behaviour.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "regex.h"
#include "textutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "ptx"

static void print_usage(void) {
    puts("Usage: ptx [OPTION]... [INPUT]...   (without -G)\n"
         "  or:  ptx -G [OPTION]... [INPUT [OUTPUT]]\n"
         "Output a permuted index, including context, of the words in the input files.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  -A, --auto-reference           output automatically generated references\n"
         "  -G, --traditional              behave more like System V 'ptx'\n"
         "  -F, --flag-truncation=STRING   use STRING for flagging line truncations.\n"
         "                                 The default is '/'\n"
         "  -M, --macro-name=STRING        macro name to use instead of 'xx'\n"
         "  -O, --format=roff              generate output as roff directives\n"
         "  -R, --right-side-refs          put references at right, not counted in -w\n"
         "  -S, --sentence-regexp=REGEXP   for end of lines or end of sentences\n"
         "  -T, --format=tex               generate output as TeX directives\n"
         "  -W, --word-regexp=REGEXP       use REGEXP to match each keyword\n"
         "  -b, --break-file=FILE          word break characters in this FILE\n"
         "  -f, --ignore-case              fold lower case to upper case for sorting\n"
         "  -g, --gap-size=NUMBER          gap size in columns between output fields\n"
         "  -i, --ignore-file=FILE         read ignore word list from FILE\n"
         "  -o, --only-file=FILE           read only word list from this FILE\n"
         "  -r, --references               first field of each line is a reference\n"
         "  -t, --typeset-mode               - not implemented -\n"
         "  -w, --width=NUMBER             output width in columns, reference excluded\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit");
}

enum {
    OPT_AUTO = 1, OPT_TRAD, OPT_FLAG, OPT_MACRO, OPT_ROFF, OPT_RIGHT, OPT_SENTENCE, OPT_TEX, OPT_WORD, OPT_BREAK, OPT_FOLD,
    OPT_GAP, OPT_IGNORE, OPT_ONLY, OPT_REFS, OPT_TYPESET, OPT_WIDTH, OPT_FORMAT
};

static const ArgsOption options[] = {
    { OPT_AUTO,     'A', "auto-reference",   ARGS_NONE },
    { OPT_TRAD,     'G', "traditional",      ARGS_NONE },
    { OPT_FLAG,     'F', "flag-truncation",  ARGS_REQUIRED },
    { OPT_MACRO,    'M', "macro-name",       ARGS_REQUIRED },
    { OPT_ROFF,     'O', NULL,               ARGS_NONE },
    { OPT_RIGHT,    'R', "right-side-refs",  ARGS_NONE },
    { OPT_SENTENCE, 'S', "sentence-regexp",  ARGS_REQUIRED },
    { OPT_TEX,      'T', NULL,               ARGS_NONE },
    { OPT_WORD,     'W', "word-regexp",      ARGS_REQUIRED },
    { OPT_BREAK,    'b', "break-file",       ARGS_REQUIRED },
    { OPT_FOLD,     'f', "ignore-case",      ARGS_NONE },
    { OPT_GAP,      'g', "gap-size",         ARGS_REQUIRED },
    { OPT_IGNORE,   'i', "ignore-file",      ARGS_REQUIRED },
    { OPT_ONLY,     'o', "only-file",        ARGS_REQUIRED },
    { OPT_REFS,     'r', "references",       ARGS_NONE },
    { OPT_TYPESET,  't', "typeset-mode",     ARGS_NONE },
    { OPT_WIDTH,    'w', "width",            ARGS_REQUIRED },
    { OPT_FORMAT,   0,   "format",           ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

/* ---- the settings ---- */

enum { FORMAT_DUMB = 0, FORMAT_ROFF, FORMAT_TEX };

typedef struct {
    unsigned char* data;
    size_t         size;
    const char*    name;                /* as shown in references; "" for standard input */
    const char*    path;
} Text;

typedef struct {
    const char* start;                  /* the keyword */
    ptrdiff_t   size;
    ptrdiff_t   left;                   /* the start of the context, relative to the keyword (<= 0) */
    ptrdiff_t   right;                  /* the end of the context, relative to the keyword */
    const char* reference;
    size_t      referenceSize;
    int         file;
    const char* end;                    /* the end of the text of the file */
} Occurrence;

typedef struct {
    const char* start;
    const char* end;
} Block;

typedef struct {
    int         gnu, autoReference, inputReference, rightReference, ignoreCase, formatSet;
    int         format;
    const char* truncation;             /* NULL for none */
    int         truncationSet;
    const char* macro;
    const char* sentenceRegexp;
    const char* wordRegexp;
    const char* breakFile;
    const char* ignoreFile;
    const char* onlyFile;
    ptrdiff_t   gap, width;
    ptrdiff_t   maxWordLength, inputReferenceMax;

    /* derived */
    ptrdiff_t   lineWidth, halfLineWidth, beforeMax, keyafterMax, referenceMax;
    unsigned char wordMap[256];
    Regex*      wordRegex;
    Regex*      sentenceRegex;
    int         sentenceGroups;

    /* the data */
    Text*       texts;
    int         textCount;
    Occurrence* occurrences;
    size_t      occurrenceCount, occurrenceCapacity;
    char**      ignoreWords;
    size_t      ignoreCount;
    char**      onlyWords;
    size_t      onlyCount;
} Ptx;

/* ---- reading the word lists ---- */

static int read_whole(const char* path, unsigned char** data, size_t* size) {
    FILE* in;

    if (text_open(PROGRAM, path, 1, &in) != 0)
        return -1;
    *data = text_slurp(in, size);
    if (in != stdin)
        fclose(in);
    if (*data == NULL) {
        fprintf(stderr, "%s: memory exhausted\n", PROGRAM);
        return -1;
    }
    return 0;
}

static int fold(const Ptx* x, int c) {
    return x->ignoreCase ? toupper(c) : c;
}

/** Compares two words (bytes with a length), folding case when -f is on. */
static int compare_text(const Ptx* x, const unsigned char* a, size_t an, const unsigned char* b, size_t bn) {
    size_t n = an < bn ? an : bn, i;

    for (i = 0; i < n; i++) {
        int ca = fold(x, a[i]), cb = fold(x, b[i]);

        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
    return an < bn ? -1 : (an > bn ? 1 : 0);
}

static int word_in_list(const Ptx* x, char** list, size_t count, const char* word, size_t size) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (compare_text(x, (const unsigned char*)list[i], strlen(list[i]), (const unsigned char*)word, size) == 0)
            return 1;
    }
    return 0;
}

/** A file with one word on each line. Returns 0, or -1 after a message. */
static int read_word_file(const char* path, char*** words, size_t* count) {
    unsigned char* data;
    size_t size, i, start = 0, capacity = 0;

    if (read_whole(path, &data, &size) != 0)
        return -1;
    *words = NULL;
    *count = 0;
    for (i = 0; i <= size; i++) {
        if (i == size || data[i] == '\n') {
            if (i > start) {
                char* word = (char*)malloc(i - start + 1);

                memcpy(word, data + start, i - start);
                word[i - start] = '\0';
                if (*count == capacity) {
                    capacity = capacity ? capacity * 2 : 16;
                    *words = (char**)realloc(*words, capacity * sizeof(char*));
                }
                (*words)[(*count)++] = word;
            }
            start = i + 1;
        }
    }
    free(data);
    return 0;
}

/* ---- the regular expressions (the syntax of Emacs: + ? * are operators, \( \) \| group) ---- */

static Regex* compile_emacs(const char* pattern, int groups, int icase) {
    size_t length = strlen(pattern), i, used = 0;
    char* translated = (char*)malloc(length * 2 + 1);
    char error[256];
    const char* list[1];
    Regex* regex;
    int inSet = 0;

    for (i = 0; i < length; i++) {
        char c = pattern[i];

        if (inSet) {
            translated[used++] = c;
            if (c == ']' && !(i > 0 && (pattern[i - 1] == '[' || (pattern[i - 1] == '^' && i > 1 && pattern[i - 2] == '['))))
                inSet = 0;
            continue;
        }
        if (c == '[') {
            inSet = 1;
            translated[used++] = c;
            if (i + 1 < length && pattern[i + 1] == '^')
                translated[used++] = pattern[++i];
            if (i + 1 < length && pattern[i + 1] == ']')
                translated[used++] = pattern[++i];
        } else if (c == '\\' && i + 1 < length) {
            char d = pattern[++i];

            if (d == '+' || d == '?') {
                translated[used++] = d;
            } else {
                translated[used++] = '\\';
                translated[used++] = d;
            }
        } else if (c == '+' || c == '?') {
            translated[used++] = '\\';
            translated[used++] = c;
        } else {
            translated[used++] = c;
        }
    }
    translated[used] = '\0';
    list[0] = translated;
    regex = regex_compile(list, 1, (groups ? REGEX_GROUPS : 0) | (icase ? REGEX_ICASE : 0), error, sizeof error);
    if (regex == NULL) {
        fprintf(stderr, "%s: %s (for regexp '", PROGRAM, error);
        for (i = 0; pattern[i] != '\0'; i++) {
            if (pattern[i] == '\\')
                fputc('\\', stderr);
            fputc(pattern[i], stderr);
        }
        fprintf(stderr, "')\n");
    }
    free(translated);
    return regex;
}

/* ---- finding the keywords ---- */

#define IS_SPACE(c) isspace((unsigned char)(c))

static void add_occurrence(Ptx* x, const Occurrence* o) {
    if (x->occurrenceCount == x->occurrenceCapacity) {
        x->occurrenceCapacity = x->occurrenceCapacity ? x->occurrenceCapacity * 2 : 256;
        x->occurrences = (Occurrence*)realloc(x->occurrences, x->occurrenceCapacity * sizeof(Occurrence));
        if (x->occurrences == NULL) {
            fprintf(stderr, "%s: memory exhausted\n", PROGRAM);
            exit(1);
        }
    }
    x->occurrences[x->occurrenceCount++] = *o;
}

/** The input of the regular expression searches over one text: decoded once. */
typedef struct {
    RegexInput input;
    const unsigned char* base;
} Searchable;

static size_t char_index(const RegexInput* input, size_t byteOffset) {
    size_t low = 0, high = input->length;

    while (low < high) {
        size_t mid = (low + high) / 2;

        if (input->offset[mid] < byteOffset)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}

/** Looks for a match at or after the byte position; the byte offsets of the match. */
static int search_bytes(const Regex* regex, const Searchable* s, size_t from, size_t* start, size_t* end, size_t* spans, size_t spanCount) {
    size_t cs, ce, c0 = char_index(&s->input, from);

    if (!regex_search(regex, &s->input, c0, &cs, &ce))
        return 0;
    if (spans != NULL) {
        size_t k;

        if (regex_groups(regex, &s->input, cs, ce, spans) == 0) {
            for (k = 2; k < spanCount; k++)
                spans[k] = (size_t)-1;
        }
        for (k = 0; k < spanCount; k++) {
            if (spans[k] != (size_t)-1)
                spans[k] = s->input.offset[spans[k]];
        }
    }
    *start = s->input.offset[cs];
    *end = s->input.offset[ce];
    return 1;
}

/** The length of a match of the word regexp at exactly this place (in bytes), or 0 for none. */
static size_t match_at(const Ptx* x, const unsigned char* at, const unsigned char* limit) {
    RegexInput input;
    size_t cs, ce, bytes = 0, n = (size_t)(limit - at);

    memset(&input, 0, sizeof input);
    if (n > 4096)
        n = 4096;
    if (regex_input_set(&input, at, n) != 0)
        return 0;
    if (regex_search(x->wordRegex, &input, 0, &cs, &ce) && cs == 0)
        bytes = input.offset[ce];
    regex_input_free(&input);
    return bytes;
}

/** GNU ptx' SKIP_SOMETHING: over a word, or over one other character. */
static const char* skip_something(const Ptx* x, const char* cursor, const char* limit) {
    if (x->wordRegex != NULL) {
        size_t count = match_at(x, (const unsigned char*)cursor, (const unsigned char*)limit);

        return cursor + (count == 0 ? 1 : count);
    }
    if (x->wordMap[(unsigned char)*cursor]) {
        while (cursor < limit && x->wordMap[(unsigned char)*cursor])
            cursor++;
        return cursor;
    }
    return cursor + 1;
}

static const char* skip_white(const char* cursor, const char* limit) {
    while (cursor < limit && IS_SPACE(*cursor))
        cursor++;
    return cursor;
}

static const char* skip_non_white(const char* cursor, const char* limit) {
    while (cursor < limit && !IS_SPACE(*cursor))
        cursor++;
    return cursor;
}

static const char* skip_white_backwards(const char* cursor, const char* start) {
    while (cursor > start && IS_SPACE(cursor[-1]))
        cursor--;
    return cursor;
}

/** Whether this word is a keyword: not in the ignore list, and in the only list when there is one. */
static int keyword_allowed(const Ptx* x, const char* word, size_t size) {
    if (x->onlyFile != NULL && !word_in_list(x, x->onlyWords, x->onlyCount, word, size))
        return 0;
    if (x->ignoreFile != NULL && word_in_list(x, x->ignoreWords, x->ignoreCount, word, size))
        return 0;
    return 1;
}

static int digits_of(long long n) {
    int d = 1;

    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d;
}

/** The default sentence end of GNU ptx: [.?!][]"')}]*\($\|\t\|  \)[ \t\n]*  Returns 1 and the match, the end of the group. */
static int default_sentence_search(const char* text, size_t size, size_t from, size_t* start, size_t* end, size_t* groupStart) {
    size_t i;

    for (i = from; i < size; i++) {
        size_t j;

        if (text[i] != '.' && text[i] != '?' && text[i] != '!')
            continue;
        j = i + 1;
        while (j < size && strchr("]\"')}", text[j]) != NULL && text[j] != '\0')
            j++;
        /* $ (before a line feed or at the end), a tab, or two spaces */
        if (j >= size || text[j] == '\n' || text[j] == '\t' || (text[j] == ' ' && j + 1 < size && text[j + 1] == ' ')) {
            size_t k = j;

            if (j < size && text[j] == '\t')
                k = j + 1;
            else if (j < size && text[j] == ' ')
                k = j + 2;
            while (k < size && (text[k] == ' ' || text[k] == '\t' || text[k] == '\n'))
                k++;
            *start = i;
            *end = k;
            *groupStart = j;
            return 1;
        }
    }
    return 0;
}

/** The reference of the line that starts at begin: its first word (NULL for a line of blanks); *refEnd is where it ends. */
static const char* line_reference(const char* begin, const char* end, const char** refEnd) {
    const char* p = begin;

    while (p < end && *p != '\n' && IS_SPACE(*p))
        p++;
    if (p >= end || *p == '\n')
        return NULL;
    *refEnd = skip_non_white(p, end);
    return p;
}

/** Finds the keywords of one text. Returns 0, or -1 after a message. */
static int find_occurrences(Ptx* x, int fileIndex) {
    Text* t = &x->texts[fileIndex];
    const char* text = (const char*)t->data;
    size_t size = t->size, nextStart = 0;
    Searchable sentences, words;
    int haveSentences = 0, haveWords = 0;
    const char* lineCursor = text;
    long long lineNumber = 1;
    const char* end = text + size;

    memset(&sentences, 0, sizeof sentences);
    memset(&words, 0, sizeof words);
    if (x->sentenceRegex != NULL) {
        regex_input_set(&sentences.input, t->data, size);
        haveSentences = 1;
    }
    if (x->wordRegex != NULL) {
        regex_input_set(&words.input, t->data, size);
        haveWords = 1;
    }
    while (nextStart < size) {
        size_t matchStart = 0, matchEnd = 0, groupStart = (size_t)-1;
        int matched;
        const char* contextStart;
        const char* skipped;
        const char* contextEnd;
        const char* referenceStart = NULL;
        const char* referenceEnd = NULL;
        const char* cursor;

        if (x->sentenceRegex != NULL) {
            size_t spans[8];

            matched = search_bytes(x->sentenceRegex, &sentences, nextStart, &matchStart, &matchEnd, x->sentenceGroups > 0 ? spans : NULL, 8);
            if (matched && x->sentenceGroups > 0 && spans[2] != (size_t)-1)
                groupStart = spans[2];
        } else if (x->gnu && !x->inputReference) {
            matched = default_sentence_search(text, size, nextStart, &matchStart, &matchEnd, &groupStart);
        } else {
            /* the default of the traditional mode and of -r: a line is a context */
            const char* nl = memchr(text + nextStart, '\n', size - nextStart);

            matched = nl != NULL;
            if (matched) {
                matchStart = (size_t)(nl - text);
                matchEnd = matchStart + 1;
            }
        }
        skipped = skip_white(text + nextStart, end);
        contextStart = text + nextStart;
        if (skipped >= end)
            break;
        if (matched) {
            if (matchEnd == matchStart || (size_t)(skipped - text) > matchStart) {
                fprintf(stderr, "%s: error: regular expression has a match of length zero: '%s'\n", PROGRAM,
                        x->sentenceRegexp != NULL ? x->sentenceRegexp : "\\n");
                return -1;
            }
            contextEnd = groupStart != (size_t)-1 ? text + groupStart : text + matchEnd;
            nextStart = matchEnd;
        } else {
            contextEnd = end;
            nextStart = size;
        }
        contextEnd = skip_white_backwards(contextEnd, skipped);
        if (x->inputReference) {
            /* the reference of a line is its first word; a context that starts with it starts after it */
            const char* lineBegin = skipped;

            while (lineBegin > text && lineBegin[-1] != '\n')
                lineBegin--;
            if (skipped == line_reference(lineBegin, end, &referenceEnd))
                contextStart = skip_white(referenceEnd, contextEnd);
        }
        cursor = contextStart;
        while (cursor < contextEnd) {
            const char* wordStart;
            const char* wordEnd;
            Occurrence o;

            if (x->wordRegex != NULL) {
                size_t ms, me;

                if (!haveWords || !search_bytes(x->wordRegex, &words, (size_t)(cursor - text), &ms, &me, NULL, 0))
                    break;
                if (text + ms >= contextEnd)
                    break;
                if (me == ms) {
                    fprintf(stderr, "%s: error: regular expression has a match of length zero: '%s'\n", PROGRAM,
                            x->wordRegexp != NULL ? x->wordRegexp : "");
                    return -1;
                }
                wordStart = text + ms;
                wordEnd = text + me;
                if (wordEnd > contextEnd)
                    wordEnd = contextEnd;
            } else {
                while (cursor < contextEnd && !x->wordMap[(unsigned char)*cursor])
                    cursor++;
                if (cursor >= contextEnd)
                    break;
                wordStart = cursor;
                wordEnd = cursor;
                while (wordEnd < contextEnd && x->wordMap[(unsigned char)*wordEnd])
                    wordEnd++;
            }
            cursor = wordEnd;
            if (x->inputReference) {
                const char* lineBegin = wordStart;
                const char* refEnd;
                const char* refBegin;

                while (lineBegin > text && lineBegin[-1] != '\n')
                    lineBegin--;
                refBegin = line_reference(lineBegin, end, &refEnd);
                referenceStart = refBegin;
                referenceEnd = refEnd;
                if (refBegin != NULL && wordStart >= refBegin && wordStart < refEnd)
                    continue;
            }
            if (wordEnd - wordStart > x->maxWordLength)
                x->maxWordLength = wordEnd - wordStart;
            if (!keyword_allowed(x, wordStart, (size_t)(wordEnd - wordStart)))
                continue;
            memset(&o, 0, sizeof o);
            o.start = wordStart;
            o.size = wordEnd - wordStart;
            o.left = contextStart - wordStart;
            o.right = contextEnd - wordStart;
            o.file = fileIndex;
            o.end = end;
            if (x->autoReference) {
                char name[VPATH_MAX + 32];
                char* copy;

                while (lineCursor < wordStart) {
                    const char* nl = memchr(lineCursor, '\n', (size_t)(wordStart - lineCursor));

                    if (nl == NULL) {
                        lineCursor = wordStart;
                        break;
                    }
                    lineNumber++;
                    lineCursor = nl + 1;
                }
                snprintf(name, sizeof name, "%s:%lld", t->name, lineNumber);
                copy = _strdup(name);
                o.reference = copy;
                o.referenceSize = strlen(copy);
            } else if (x->inputReference) {
                o.reference = referenceStart;
                o.referenceSize = (size_t)(referenceEnd - referenceStart);
                if ((ptrdiff_t)o.referenceSize > x->inputReferenceMax)
                    x->inputReferenceMax = (ptrdiff_t)o.referenceSize;
            }
            add_occurrence(x, &o);
        }
    }
    regex_input_free(&sentences.input);
    regex_input_free(&words.input);
    (void)haveSentences;
    return 0;
}

/* ---- sorting ---- */

static const Ptx* sortContext;

static int compare_occurrences(const void* pa, const void* pb) {
    const Occurrence* a = (const Occurrence*)pa;
    const Occurrence* b = (const Occurrence*)pb;
    int value = compare_text(sortContext, (const unsigned char*)a->start, (size_t)a->size, (const unsigned char*)b->start, (size_t)b->size);

    if (value != 0)
        return value;
    if (a->file != b->file)
        return a->file < b->file ? -1 : 1;
    return a->start < b->start ? -1 : (a->start > b->start ? 1 : 0);
}

/* ---- the output ---- */

typedef struct {
    Block keyafter, before, tail, head, keyword, after, reference;
    int   keyafterTruncation, beforeTruncation, tailTruncation, headTruncation;
} Fields;

static void print_white_space(ptrdiff_t n) {
    while (n-- > 0)
        _putchar_nolock(' ');
}

static void print_field(const Ptx* x, Block field) {
    const char* cursor;

    for (cursor = field.start; cursor < field.end; cursor++) {
        unsigned char c = (unsigned char)*cursor;

        if (x->format == FORMAT_ROFF) {
            if (c == '"')
                _putchar_nolock('"');
            _putchar_nolock(IS_SPACE(c) ? ' ' : c);
        } else if (x->format == FORMAT_TEX) {
            switch (c) {
            case '\\': fputs("\\backslash{}", stdout); break;
            case '$': case '%': case '&': case '#': case '_': _putchar_nolock('\\'); _putchar_nolock(c); break;
            case '{': fputs("$\\{$", stdout); break;
            case '}': fputs("$\\}$", stdout); break;
            default: _putchar_nolock(IS_SPACE(c) ? ' ' : c);
            }
        } else {
            _putchar_nolock(IS_SPACE(c) ? ' ' : c);
        }
    }
}

static ptrdiff_t truncation_length(const Ptx* x) {
    return x->truncation != NULL ? (ptrdiff_t)strlen(x->truncation) : 0;
}

static void define_all_fields(const Ptx* x, const Occurrence* occurs, Fields* f) {
    ptrdiff_t tailMax, headMax;
    const char* cursor;
    const char* leftContextStart;
    const char* leftFieldContextStart;
    const char* rightContextEnd;
    const char* leftFieldStart;
    const char* textStart = (const char*)x->texts[occurs->file].data;
    const char* textEnd = occurs->end;

    memset(f, 0, sizeof *f);
    f->keyafter.start = occurs->start;
    f->keyafter.end = f->keyafter.start + occurs->size;
    leftContextStart = f->keyafter.start + occurs->left;
    leftFieldContextStart = leftContextStart;
    leftContextStart = skip_white(leftContextStart, f->keyafter.start);
    rightContextEnd = f->keyafter.start + occurs->right;

    cursor = f->keyafter.end;
    while (cursor < rightContextEnd && cursor <= f->keyafter.start + x->keyafterMax) {
        f->keyafter.end = cursor;
        cursor = skip_something(x, cursor, rightContextEnd);
    }
    if (cursor <= f->keyafter.start + x->keyafterMax)
        f->keyafter.end = cursor;

    f->keyafterTruncation = x->truncation != NULL && f->keyafter.end < rightContextEnd;
    f->keyafter.end = skip_white_backwards(f->keyafter.end, f->keyafter.start);

    /* a wide left context: begin a little before the place where the left field could start */
    if (-occurs->left > x->halfLineWidth + x->maxWordLength) {
        leftFieldStart = f->keyafter.start - (x->halfLineWidth + x->maxWordLength);
        leftFieldStart = skip_something(x, leftFieldStart, f->keyafter.start);
    } else {
        leftFieldStart = leftFieldContextStart;
    }

    f->before.start = leftFieldStart;
    f->before.end = f->keyafter.start;
    f->before.end = skip_white_backwards(f->before.end, f->before.start);
    f->before.start = skip_white(f->before.start, f->before.end);
    if (leftFieldStart == leftFieldContextStart && f->before.start > leftContextStart)
        leftContextStart = f->before.start;
    while (f->before.start + x->beforeMax < f->before.end)
        f->before.start = skip_something(x, f->before.start, f->before.end);
    f->beforeTruncation = x->truncation != NULL && leftContextStart < f->before.start;
    f->before.start = skip_white(f->before.start, textEnd);

    /* the tail: what follows the keyafter field and fits into what the before field leaves free */
    tailMax = x->beforeMax - (f->before.end - f->before.start) - x->gap;
    if (tailMax > 0) {
        f->tail.start = skip_white(f->keyafter.end, textEnd);
        f->tail.end = f->tail.start;
        cursor = f->tail.end;
        while (cursor < rightContextEnd && cursor < f->tail.start + tailMax) {
            f->tail.end = cursor;
            cursor = skip_something(x, cursor, rightContextEnd);
        }
        if (cursor < f->tail.start + tailMax)
            f->tail.end = cursor;
        if (f->tail.end > f->tail.start) {
            f->keyafterTruncation = 0;
            f->tailTruncation = x->truncation != NULL && f->tail.end < rightContextEnd;
        } else {
            f->tailTruncation = 0;
        }
        f->tail.end = skip_white_backwards(f->tail.end, f->tail.start);
    } else {
        f->tail.start = f->tail.end = NULL;
        f->tailTruncation = 0;
    }

    /* the head: what is in front of the before field and fits into what the keyafter field leaves free */
    headMax = x->keyafterMax - (f->keyafter.end - f->keyafter.start) - x->gap;
    if (headMax > 0) {
        f->head.end = skip_white_backwards(f->before.start, textStart);
        f->head.start = leftFieldStart;
        while (f->head.start + headMax < f->head.end)
            f->head.start = skip_something(x, f->head.start, f->head.end);
        if (f->head.end > f->head.start) {
            f->beforeTruncation = 0;
            f->headTruncation = x->truncation != NULL && leftFieldContextStart < f->head.start;
        } else {
            f->headTruncation = 0;
        }
        f->head.start = skip_white(f->head.start, f->head.end);
    } else {
        f->head.start = f->head.end = NULL;
        f->headTruncation = 0;
    }

    if (x->format == FORMAT_TEX) {
        f->keyword.start = occurs->start;
        f->keyword.end = occurs->start + occurs->size;
        f->after.start = f->keyword.end;
        f->after.end = f->keyafter.end;
    }
    f->reference.start = occurs->reference;
    f->reference.end = occurs->reference != NULL ? occurs->reference + occurs->referenceSize : NULL;
}

static void put_truncation(const Ptx* x, int on) {
    if (on && x->truncation != NULL && x->format != FORMAT_TEX)
        fputs(x->truncation, stdout);
}

static void output_dumb(const Ptx* x, const Fields* f) {
    ptrdiff_t referenceLength = f->reference.end - f->reference.start;
    ptrdiff_t tl = truncation_length(x);
    int hasReference = x->autoReference || x->inputReference;

    if (!x->rightReference) {
        if (x->autoReference) {
            print_field(x, f->reference);
            _putchar_nolock(':');
            print_white_space(x->referenceMax + x->gap - referenceLength - 1);
        } else {
            print_field(x, f->reference);
            print_white_space(x->referenceMax + x->gap - referenceLength);
        }
    }
    if (f->tail.start < f->tail.end) {
        print_field(x, f->tail);
        put_truncation(x, f->tailTruncation);
        print_white_space(x->halfLineWidth - x->gap - (f->tail.end - f->tail.start) - (f->tailTruncation ? tl : 0) -
                          (f->before.end - f->before.start) - (f->beforeTruncation ? tl : 0));
        put_truncation(x, f->beforeTruncation);
        print_field(x, f->before);
    } else {
        print_white_space(x->halfLineWidth - x->gap - (f->before.end - f->before.start) - (f->beforeTruncation ? tl : 0));
        put_truncation(x, f->beforeTruncation);
        print_field(x, f->before);
    }
    print_white_space(x->gap);
    print_field(x, f->keyafter);
    put_truncation(x, f->keyafterTruncation);
    if (f->head.start < f->head.end) {
        print_white_space(x->halfLineWidth - (f->keyafter.end - f->keyafter.start) - (f->keyafterTruncation ? tl : 0) -
                          (f->head.end - f->head.start) - (f->headTruncation ? tl : 0));
        put_truncation(x, f->headTruncation);
        print_field(x, f->head);
    } else if (hasReference && x->rightReference) {
        print_white_space(x->halfLineWidth - (f->keyafter.end - f->keyafter.start) - (f->keyafterTruncation ? tl : 0));
    }
    if (hasReference && x->rightReference) {
        print_white_space(x->gap);
        print_field(x, f->reference);
    }
    _putchar_nolock('\n');
}

static void output_roff(const Ptx* x, const Fields* f) {
    printf(".%s \"", x->macro);
    print_field(x, f->tail);
    put_truncation(x, f->tailTruncation);
    fputs("\" \"", stdout);
    put_truncation(x, f->beforeTruncation);
    print_field(x, f->before);
    fputs("\" \"", stdout);
    print_field(x, f->keyafter);
    put_truncation(x, f->keyafterTruncation);
    fputs("\" \"", stdout);
    put_truncation(x, f->headTruncation);
    print_field(x, f->head);
    _putchar_nolock('"');
    if (x->autoReference || x->inputReference) {
        fputs(" \"", stdout);
        print_field(x, f->reference);
        _putchar_nolock('"');
    }
    _putchar_nolock('\n');
}

static void output_tex(const Ptx* x, const Fields* f) {
    printf("\\%s ", x->macro);
    _putchar_nolock('{');
    print_field(x, f->tail);
    put_truncation(x, f->tailTruncation);
    fputs("}{", stdout);
    put_truncation(x, f->beforeTruncation);
    print_field(x, f->before);
    fputs("}{", stdout);
    print_field(x, f->keyword);
    fputs("}{", stdout);
    print_field(x, f->after);
    put_truncation(x, f->keyafterTruncation);
    fputs("}{", stdout);
    put_truncation(x, f->headTruncation);
    print_field(x, f->head);
    _putchar_nolock('}');
    if (x->autoReference || x->inputReference) {
        _putchar_nolock('{');
        print_field(x, f->reference);
        _putchar_nolock('}');
    }
    _putchar_nolock('\n');
}

static void fix_output_parameters(Ptx* x) {
    x->referenceMax = 0;
    if (x->autoReference) {
        int file;

        for (file = 0; file < x->textCount; file++) {
            long long lines = 1;
            size_t k;
            ptrdiff_t width;

            for (k = 0; k < x->texts[file].size; k++) {
                if (x->texts[file].data[k] == '\n')
                    lines++;
            }
            width = (ptrdiff_t)strlen(x->texts[file].name) + 1 + digits_of(lines);
            if (width > x->referenceMax)
                x->referenceMax = width;
        }
    } else if (x->inputReference) {
        x->referenceMax = x->inputReferenceMax;
    }
    x->lineWidth = x->width;
    if ((x->autoReference || x->inputReference) && !x->rightReference)
        x->lineWidth -= x->referenceMax + x->gap;
    if (x->lineWidth < 0)
        x->lineWidth = 0;
    x->halfLineWidth = x->lineWidth / 2;
    x->beforeMax = x->halfLineWidth - x->gap;
    x->keyafterMax = x->halfLineWidth;
    if (x->gnu) {
        x->beforeMax -= 2 * truncation_length(x);
        if (x->beforeMax < 0)
            x->beforeMax = 0;
        x->keyafterMax -= 2 * truncation_length(x);
    } else {
        x->keyafterMax -= 2 * truncation_length(x) + 1;
    }
}

/* ---- the options ---- */

static int parse_positive(const char* text, const char* what, ptrdiff_t* out) {
    char* end;
    long long value;

    errno = 0;
    value = strtoll(text, &end, 10);
    if (end == text || *end != '\0' || value <= 0 || errno != 0 || value > INT_MAX) {
        fprintf(stderr, "%s: invalid %s: '%s'\n", PROGRAM, what, text);
        return -1;
    }
    *out = (ptrdiff_t)value;
    return 0;
}

/** A string with the escapes of C taken out (\n, \t, \\, octal \NNN, hex \xHH ...); NULL for an empty one. */
static const char* unescape(const char* in) {
    char* out = (char*)malloc(strlen(in) + 1);
    size_t used = 0;

    while (*in != '\0') {
        if (*in != '\\' || in[1] == '\0') {
            out[used++] = *in++;
            continue;
        }
        in++;
        switch (*in) {
        case 'a': out[used++] = '\a'; in++; break;
        case 'b': out[used++] = '\b'; in++; break;
        case 'f': out[used++] = '\f'; in++; break;
        case 'n': out[used++] = '\n'; in++; break;
        case 'r': out[used++] = '\r'; in++; break;
        case 't': out[used++] = '\t'; in++; break;
        case 'v': out[used++] = '\v'; in++; break;
        case 'x': {
            int value = 0, digits = 0;

            in++;
            while (digits < 2 && isxdigit((unsigned char)*in)) {
                value = value * 16 + (isdigit((unsigned char)*in) ? *in - '0' : tolower((unsigned char)*in) - 'a' + 10);
                in++;
                digits++;
            }
            out[used++] = (char)value;
            break;
        }
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
            int value = 0, digits = 0;

            while (digits < 3 && *in >= '0' && *in <= '7') {
                value = value * 8 + (*in - '0');
                in++;
                digits++;
            }
            out[used++] = (char)value;
            break;
        }
        default: out[used++] = *in++; break;
        }
    }
    out[used] = '\0';
    if (used == 0) {
        free(out);
        return NULL;
    }
    return out;
}

static int on_option(int id, const char* value, void* context) {
    Ptx* x = (Ptx*)context;

    switch (id) {
    case OPT_AUTO: x->autoReference = 1; break;
    case OPT_TRAD: x->gnu = 0; break;
    case OPT_FLAG: x->truncation = unescape(value); x->truncationSet = 1; break;
    case OPT_MACRO: x->macro = value; break;
    case OPT_ROFF: x->format = FORMAT_ROFF; x->formatSet = 1; break;
    case OPT_TEX: x->format = FORMAT_TEX; x->formatSet = 1; break;
    case OPT_RIGHT: x->rightReference = 1; break;
    case OPT_SENTENCE: x->sentenceRegexp = value; break;
    case OPT_WORD: x->wordRegexp = value; break;
    case OPT_BREAK: x->breakFile = value; break;
    case OPT_FOLD: x->ignoreCase = 1; break;
    case OPT_GAP:
        if (parse_positive(value, "gap width", &x->gap) != 0)
            return -1;
        break;
    case OPT_IGNORE: x->ignoreFile = value; break;
    case OPT_ONLY: x->onlyFile = value; break;
    case OPT_REFS: x->inputReference = 1; break;
    case OPT_TYPESET: break;
    case OPT_WIDTH:
        if (parse_positive(value, "line width", &x->width) != 0)
            return -1;
        break;
    case OPT_FORMAT:
        if (strcmp(value, "roff") == 0) {
            x->format = FORMAT_ROFF;
        } else if (strcmp(value, "tex") == 0) {
            x->format = FORMAT_TEX;
        } else {
            fprintf(stderr, "%s: invalid argument '%s' for '--format'\nValid arguments are:\n  - 'roff'\n  - 'tex'\n", PROGRAM, value);
            args_usage_error(&spec);
            return -1;
        }
        x->formatSet = 1;
        break;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    Ptx x;
    const char** operands;
    int count, result, i, status = 0;
    const char* outputPath = NULL;
    FILE* out = stdout;

    memset(&x, 0, sizeof x);
    x.gnu = 1;
    x.gap = 3;
    x.width = 72;
    x.macro = "xx";
    x.truncation = "/";

    text_binary_streams();
    result = args_parse(&spec, argc, argv, on_option, &x, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    if (!x.gnu && !x.formatSet)
        x.format = FORMAT_ROFF;

    /* the word characters */
    if (x.wordRegexp != NULL && x.wordRegexp[0] == '\0')
        x.wordRegexp = NULL;
    if (x.breakFile != NULL) {
        unsigned char* data;
        size_t size, k;

        if (read_whole(x.breakFile, &data, &size) != 0)
            return 1;
        memset(x.wordMap, 1, sizeof x.wordMap);
        for (k = 0; k < size; k++)
            x.wordMap[data[k]] = 0;
        free(data);
    } else {
        int c;

        for (c = 0; c < 256; c++) {
            if (x.gnu)
                x.wordMap[c] = (unsigned char)((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
            else
                x.wordMap[c] = (unsigned char)!isspace(c);
        }
    }
    if (x.wordRegexp != NULL) {
        x.wordRegex = compile_emacs(x.wordRegexp, 0, x.ignoreCase);
        if (x.wordRegex == NULL)
            return 1;
    }
    if (x.sentenceRegexp != NULL && x.sentenceRegexp[0] != '\0') {
        x.sentenceRegex = compile_emacs(x.sentenceRegexp, 1, x.ignoreCase);
        if (x.sentenceRegex == NULL)
            return 1;
        x.sentenceGroups = (int)regex_group_count(x.sentenceRegex);
    } else {
        x.sentenceRegexp = NULL;
    }
    if (x.ignoreFile != NULL && read_word_file(x.ignoreFile, &x.ignoreWords, &x.ignoreCount) != 0)
        return 1;
    if (x.onlyFile != NULL && read_word_file(x.onlyFile, &x.onlyWords, &x.onlyCount) != 0)
        return 1;

    /* the files */
    if (!x.gnu && count > 2) {
        fprintf(stderr, "%s: extra operand '%s'\n", PROGRAM, operands[2]);
        args_usage_error(&spec);
        return 1;
    }
    if (!x.gnu && count == 2) {
        outputPath = operands[1];
        count = 1;
    }
    x.textCount = count > 0 ? count : 1;
    x.texts = (Text*)calloc((size_t)x.textCount, sizeof(Text));
    for (i = 0; i < x.textCount; i++) {
        const char* name = count > 0 ? operands[i] : "-";
        unsigned char* data;
        size_t size;

        if (read_whole(name, &data, &size) != 0) {
            status = 1;
            x.texts[i].data = (unsigned char*)malloc(1);
            x.texts[i].size = 0;
            x.texts[i].name = "";
            continue;
        }
        x.texts[i].data = data;
        x.texts[i].size = size;
        x.texts[i].path = name;
        x.texts[i].name = strcmp(name, "-") == 0 ? "" : name;
    }
    if (status != 0)
        return status;
    for (i = 0; i < x.textCount; i++) {
        if (find_occurrences(&x, i) != 0)
            return 1;
    }

    if (outputPath != NULL && strcmp(outputPath, "-") != 0) {
        char native[VPATH_MAX];

        if (vpath_to_native(outputPath, native, sizeof native) != 0 || (out = fopen(native, "wb")) == NULL) {
            fprintf(stderr, "%s: %s: %s\n", PROGRAM, outputPath, strerror(errno));
            return 1;
        }
        if (_dup2(_fileno(out), _fileno(stdout)) < 0) {
            fprintf(stderr, "%s: %s: %s\n", PROGRAM, outputPath, strerror(errno));
            return 1;
        }
    }
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);

    sortContext = &x;
    qsort(x.occurrences, x.occurrenceCount, sizeof(Occurrence), compare_occurrences);
    fix_output_parameters(&x);
    {
        size_t k;
        Fields f;

        for (k = 0; k < x.occurrenceCount; k++) {
            define_all_fields(&x, &x.occurrences[k], &f);
            if (x.format == FORMAT_ROFF)
                output_roff(&x, &f);
            else if (x.format == FORMAT_TEX)
                output_tex(&x, &f);
            else
                output_dumb(&x, &f);
        }
    }
    fflush(stdout);
    return status;
}
