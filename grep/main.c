#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "regex.h"
#include "textutil.h"
#include "vec.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "grep"

static void print_usage(void) {
    puts("Usage: grep [OPTION]... PATTERNS [FILE]...\n"
         "Search for PATTERNS in each FILE (standard input when there is none).\n"
         "Example: grep -i 'hello world' menu.h main.c\n"
         "\n"
         "Pattern selection and interpretation:\n"
         "  -E, --extended-regexp     PATTERNS are extended regular expressions\n"
         "  -F, --fixed-strings       PATTERNS are strings\n"
         "  -G, --basic-regexp        PATTERNS are basic regular expressions (the default)\n"
         "  -e, --regexp=PATTERNS     use PATTERNS for matching; may be given more than once\n"
         "  -f, --file=FILE           take PATTERNS from FILE, one per line\n"
         "  -i, --ignore-case         ignore case distinctions\n"
         "  -w, --word-regexp         match only whole words\n"
         "  -x, --line-regexp         match only whole lines\n"
         "\n"
         "Miscellaneous:\n"
         "  -s, --no-messages         suppress error messages about files\n"
         "  -v, --invert-match        select non-matching lines\n"
         "      --help                display this help and exit\n"
         "      --version             output version information and exit\n"
         "\n"
         "Output control:\n"
         "  -m, --max-count=NUM       stop after NUM selected lines\n"
         "  -b, --byte-offset         print the byte offset with output lines\n"
         "  -n, --line-number         print line number with output lines\n"
         "      --label=LABEL         use LABEL as the file name for standard input\n"
         "  -H, --with-filename       print the file name for each match\n"
         "  -h, --no-filename         suppress the file name prefix on output\n"
         "  -o, --only-matching       show only the part of a line matching PATTERNS\n"
         "  -q, --quiet, --silent     suppress all normal output\n"
         "      --binary-files=TYPE   binary, text or without-match\n"
         "  -a, --text                same as --binary-files=text\n"
         "  -I                        same as --binary-files=without-match\n"
         "  -r, --recursive           read all files under each directory\n"
         "  -R, --dereference-recursive  likewise (links are not followed here)\n"
         "      --include=GLOB        search only files whose base name matches GLOB\n"
         "      --exclude=GLOB        skip files whose base name matches GLOB\n"
         "      --exclude-dir=GLOB    skip directories whose base name matches GLOB\n"
         "  -L, --files-without-match print only names of FILEs with no selected lines\n"
         "  -l, --files-with-matches  print only names of FILEs with selected lines\n"
         "  -c, --count               print only a count of selected lines per FILE\n"
         "  -Z, --null                print 0 byte after FILE name\n"
         "  -z, --null-data           a data line ends in 0 byte, not newline\n"
         "\n"
         "Context control:\n"
         "  -B, --before-context=NUM  print NUM lines of leading context\n"
         "  -A, --after-context=NUM   print NUM lines of trailing context\n"
         "  -C, --context=NUM         print NUM lines of output context\n"
         "  -NUM                      same as --context=NUM\n"
         "\n"
         "A CR in front of the line feed is part of the line ending: \"foo$\" matches \"foo\\r\\n\" and\n"
         "the ending is written back as it was. Text is UTF-8: \".\" matches one character.\n"
         "Exit status is 0 if a line is selected, 1 if no line was selected, and 2 if an error\n"
         "occurred (but 0 if -q is used and a line is selected).");
}

enum {
    OPT_EXTENDED = 1, OPT_FIXED, OPT_BASIC, OPT_PERL, OPT_REGEXP, OPT_FILE, OPT_ICASE, OPT_NO_ICASE, OPT_WORD,
    OPT_LINE, OPT_NO_MESSAGES, OPT_INVERT, OPT_MAX_COUNT, OPT_BYTE_OFFSET, OPT_LINE_NUMBER, OPT_LABEL,
    OPT_WITH_FILENAME, OPT_NO_FILENAME, OPT_ONLY_MATCHING, OPT_QUIET, OPT_BINARY_FILES, OPT_TEXT, OPT_NO_BINARY,
    OPT_RECURSIVE, OPT_INCLUDE, OPT_EXCLUDE, OPT_EXCLUDE_DIR, OPT_FILES_WITHOUT, OPT_FILES_WITH, OPT_COUNT,
    OPT_NULL, OPT_NULL_DATA, OPT_BEFORE, OPT_AFTER, OPT_CONTEXT, OPT_VERSION, OPT_ACCEPTED, OPT_DIRECTORIES
};

static const ArgsOption optionList[] = {
    { OPT_EXTENDED,    'E', "extended-regexp",       ARGS_NONE },
    { OPT_FIXED,       'F', "fixed-strings",         ARGS_NONE },
    { OPT_BASIC,       'G', "basic-regexp",          ARGS_NONE },
    { OPT_PERL,        'P', "perl-regexp",           ARGS_NONE },
    { OPT_REGEXP,      'e', "regexp",                ARGS_REQUIRED },
    { OPT_FILE,        'f', "file",                  ARGS_REQUIRED },
    { OPT_ICASE,       'i', "ignore-case",           ARGS_NONE },
    { OPT_ICASE,       'y', NULL,                    ARGS_NONE },
    { OPT_NO_ICASE,    0,   "no-ignore-case",        ARGS_NONE },
    { OPT_WORD,        'w', "word-regexp",           ARGS_NONE },
    { OPT_LINE,        'x', "line-regexp",           ARGS_NONE },
    { OPT_NO_MESSAGES, 's', "no-messages",           ARGS_NONE },
    { OPT_INVERT,      'v', "invert-match",          ARGS_NONE },
    { OPT_VERSION,     'V', NULL,                    ARGS_NONE },
    { OPT_MAX_COUNT,   'm', "max-count",             ARGS_REQUIRED },
    { OPT_BYTE_OFFSET, 'b', "byte-offset",           ARGS_NONE },
    { OPT_LINE_NUMBER, 'n', "line-number",           ARGS_NONE },
    { OPT_LABEL,       0,   "label",                 ARGS_REQUIRED },
    { OPT_WITH_FILENAME, 'H', "with-filename",       ARGS_NONE },
    { OPT_NO_FILENAME, 'h', "no-filename",           ARGS_NONE },
    { OPT_ONLY_MATCHING, 'o', "only-matching",       ARGS_NONE },
    { OPT_QUIET,       'q', "quiet",                 ARGS_NONE },
    { OPT_QUIET,       0,   "silent",                ARGS_NONE },
    { OPT_BINARY_FILES, 0,  "binary-files",          ARGS_REQUIRED },
    { OPT_TEXT,        'a', "text",                  ARGS_NONE },
    { OPT_NO_BINARY,   'I', NULL,                    ARGS_NONE },
    { OPT_RECURSIVE,   'r', "recursive",             ARGS_NONE },
    { OPT_RECURSIVE,   'R', "dereference-recursive", ARGS_NONE },
    { OPT_INCLUDE,     0,   "include",               ARGS_REQUIRED },
    { OPT_EXCLUDE,     0,   "exclude",               ARGS_REQUIRED },
    { OPT_EXCLUDE_DIR, 0,   "exclude-dir",           ARGS_REQUIRED },
    { OPT_FILES_WITHOUT, 'L', "files-without-match", ARGS_NONE },
    { OPT_FILES_WITH,  'l', "files-with-matches",    ARGS_NONE },
    { OPT_COUNT,       'c', "count",                 ARGS_NONE },
    { OPT_NULL,        'Z', "null",                  ARGS_NONE },
    { OPT_NULL_DATA,   'z', "null-data",             ARGS_NONE },
    { OPT_BEFORE,      'B', "before-context",        ARGS_REQUIRED },
    { OPT_AFTER,       'A', "after-context",         ARGS_REQUIRED },
    { OPT_CONTEXT,     'C', "context",               ARGS_REQUIRED },
    { OPT_ACCEPTED,    'U', "binary",                ARGS_NONE },
    { OPT_ACCEPTED,    0,   "line-buffered",         ARGS_NONE },
    { OPT_ACCEPTED,    0,   "color",                 ARGS_OPTIONAL },
    { OPT_ACCEPTED,    0,   "colour",                ARGS_OPTIONAL },
    { OPT_ACCEPTED,    'T', "initial-tab",           ARGS_NONE },
    { OPT_DIRECTORIES, 'd', "directories",           ARGS_REQUIRED },
    { OPT_ACCEPTED,    'D', "devices",               ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, optionList, 2, 0, 1 };

/* ---- what was asked ----------------------------------------------------------------- */

typedef struct {
    int         extended, fixed, icase, invert, word, line;
    int         count, listMatching, listNonMatching, quiet, noMessages;
    int         onlyMatching, byteOffset, lineNumber, nullAfterName, nullData;
    int         withFilename;               /* -1 not said, 0 -h, 1 -H */
    int         recursive;
    int         text, withoutMatch;         /* what to do with binary files */
    long        maxCount;                   /* -1: no limit */
    long        after, before, context;     /* -1: not said */
    const char* label;
    const char** patterns;                  /* from -e, -f, and the first operand */
    size_t      patternCount, patternCapacity;
    int         patternGiven;
    const char** include;
    size_t      includeCount, includeCapacity;
    const char** exclude;
    size_t      excludeCount, excludeCapacity;
    const char** excludeDir;
    size_t      excludeDirCount, excludeDirCapacity;
    int         skipDirectories;            /* -d skip */
    int         failed;                     /* a message was printed by the option handler */
} Config;

static int add_pattern_text(Config* config, const char* text) {
    // a pattern with line feeds in it is several patterns
    char* copy = _strdup(text);
    char* part;
    char* next;

    if (copy == NULL)
        return -1;
    for (part = copy;; part = next) {
        next = strchr(part, '\n');
        if (next != NULL)
            *next++ = '\0';
        if (VEC_RESERVE(config->patterns, config->patternCapacity, config->patternCount + 1) != 0)
            return -1;
        config->patterns[config->patternCount] = _strdup(part);
        if (config->patterns[config->patternCount] == NULL)
            return -1;
        config->patternCount++;
        if (next == NULL)
            break;
    }
    free(copy);
    return 0;
}

/** -f FILE: one pattern per line; an empty file means no pattern, which matches nothing. */
static int add_pattern_file(Config* config, const char* path) {
    FILE* in;
    unsigned char* text;
    size_t length, offset = 0;
    TextLine line;

    if (text_open(PROGRAM, path, 1, &in) != 0)
        return -1;
    text = text_slurp(in, &length);
    if (in != stdin)
        fclose(in);
    if (text == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return -1;
    }

    while (text_next_line(text, length, &offset, &line)) {
        char* one = malloc(line.length + 1);
        if (one == NULL || VEC_RESERVE(config->patterns, config->patternCapacity, config->patternCount + 1) != 0) {
            free(one);
            free(text);
            return -1;
        }
        memcpy(one, line.content, line.length);
        one[line.length] = '\0';
        config->patterns[config->patternCount++] = one;
    }
    free(text);
    return 0;
}

static int parse_number(const char* text, long* out, const char* what) {
    char* end;
    long value = strtol(text, &end, 10);

    if (text[0] == '\0' || *end != '\0' || value < 0) {
        if (strcmp(what, "max") == 0)
            fprintf(stderr, PROGRAM ": invalid max count\n");
        else
            fprintf(stderr, PROGRAM ": %s: invalid context length argument\n", text);
        return -1;
    }
    *out = value;
    return 0;
}

static int add_to_list(const char*** list, size_t* count, size_t* capacity, const char* value) {
    if (VEC_RESERVE(*list, *capacity, *count + 1) != 0)
        return -1;
    (*list)[(*count)++] = value;
    return 0;
}

static int on_option(int id, const char* value, void* context) {
    Config* config = context;
    (void)context;

    switch (id) {
        case OPT_EXTENDED:   config->extended = 1; config->fixed = 0; return 0;
        case OPT_FIXED:      config->fixed = 1; config->extended = 0; return 0;
        case OPT_BASIC:      config->fixed = config->extended = 0; return 0;
        case OPT_PERL:
            fprintf(stderr, PROGRAM ": Perl matching is not supported\n");
            return -1;
        case OPT_REGEXP:     config->patternGiven = 1; return add_pattern_text(config, value);
        case OPT_FILE:       config->patternGiven = 1; return add_pattern_file(config, value);
        case OPT_ICASE:      config->icase = 1; return 0;
        case OPT_NO_ICASE:   config->icase = 0; return 0;
        case OPT_WORD:       config->word = 1; return 0;
        case OPT_LINE:       config->line = 1; return 0;
        case OPT_NO_MESSAGES: config->noMessages = 1; return 0;
        case OPT_INVERT:     config->invert = 1; return 0;
        case OPT_VERSION:
            puts(PROGRAM " (Winshell) " VERSION);
            exit(0);
        case OPT_MAX_COUNT:  return parse_number(value, &config->maxCount, "max");
        case OPT_BYTE_OFFSET: config->byteOffset = 1; return 0;
        case OPT_LINE_NUMBER: config->lineNumber = 1; return 0;
        case OPT_LABEL:      config->label = value; return 0;
        case OPT_WITH_FILENAME: config->withFilename = 1; return 0;
        case OPT_NO_FILENAME: config->withFilename = 0; return 0;
        case OPT_ONLY_MATCHING: config->onlyMatching = 1; return 0;
        case OPT_QUIET:      config->quiet = 1; return 0;
        case OPT_BINARY_FILES:
            if (strcmp(value, "text") == 0) {
                config->text = 1;
            } else if (strcmp(value, "without-match") == 0) {
                config->withoutMatch = 1;
            } else if (strcmp(value, "binary") != 0) {
                fprintf(stderr, PROGRAM ": unknown binary-files type\n");
                return -1;
            }
            return 0;
        case OPT_TEXT:       config->text = 1; return 0;
        case OPT_NO_BINARY:  config->withoutMatch = 1; return 0;
        case OPT_RECURSIVE:  config->recursive = 1; return 0;
        case OPT_INCLUDE:
            return add_to_list(&config->include, &config->includeCount, &config->includeCapacity, value);
        case OPT_EXCLUDE:
            return add_to_list(&config->exclude, &config->excludeCount, &config->excludeCapacity, value);
        case OPT_EXCLUDE_DIR:
            return add_to_list(&config->excludeDir, &config->excludeDirCount, &config->excludeDirCapacity, value);
        case OPT_FILES_WITHOUT: config->listNonMatching = 1; config->listMatching = 0; return 0;
        case OPT_FILES_WITH:   config->listMatching = 1; config->listNonMatching = 0; return 0;
        case OPT_COUNT:      config->count = 1; return 0;
        case OPT_NULL:       config->nullAfterName = 1; return 0;
        case OPT_NULL_DATA:  config->nullData = 1; return 0;
        case OPT_BEFORE:     return parse_number(value, &config->before, "context");
        case OPT_AFTER:      return parse_number(value, &config->after, "context");
        case OPT_CONTEXT:
        case ARGS_NUMBER:    return parse_number(value, &config->context, "context");
        case OPT_DIRECTORIES:
            if (strcmp(value, "recurse") == 0) {
                config->recursive = 1;
            } else if (strcmp(value, "skip") == 0) {
                config->skipDirectories = 1;
            } else if (strcmp(value, "read") != 0) {
                fprintf(stderr, PROGRAM ": invalid argument '%s' for '--directories'\n", value);
                return -1;
            }
            return 0;
        default:
            return 0;
    }
}

/* ---- glob names ---------------------------------------------------------------------- */

/** A small wildcard match (* ? [...]) for --include and friends; case is ignored as Windows names are. */
static int name_matches(const char* pattern, const char* name) {
    for (; *pattern != '\0'; pattern++, name++) {
        if (*pattern == '*') {
            while (pattern[1] == '*')
                pattern++;
            if (pattern[1] == '\0')
                return 1;
            for (;; name++) {
                if (name_matches(pattern + 1, name))
                    return 1;
                if (*name == '\0')
                    return 0;
            }
        }
        if (*name == '\0')
            return 0;
        if (*pattern == '?')
            continue;
        if (*pattern == '[') {
            const char* close = strchr(pattern + 2, ']');
            int negate = pattern[1] == '!' || pattern[1] == '^', hit = 0;
            const char* q = pattern + 1 + negate;

            if (close != NULL) {
                for (; q < close; q++) {
                    if (q[1] == '-' && q + 2 < close) {
                        if (tolower((unsigned char)*name) >= tolower((unsigned char)q[0]) &&
                            tolower((unsigned char)*name) <= tolower((unsigned char)q[2]))
                            hit = 1;
                        q += 2;
                    } else if (tolower((unsigned char)*q) == tolower((unsigned char)*name)) {
                        hit = 1;
                    }
                }
                if (hit == negate)
                    return 0;
                pattern = close;
                continue;
            }
        }
        if (*pattern == '\\' && pattern[1] != '\0')
            pattern++;
        if (tolower((unsigned char)*pattern) != tolower((unsigned char)*name))
            return 0;
    }
    return *name == '\0';
}

static int matches_any(const char** list, size_t count, const char* name) {
    size_t i;
    for (i = 0; i < count; i++)
        if (name_matches(list[i], name))
            return 1;
    return 0;
}

/** Whether a file is to be searched, by its base name and the --include and --exclude lists. */
static int file_selected(Config* config, const char* name) {
    char base[VPATH_MAX];

    fs_basename(name, base, sizeof base);
    if (config->includeCount > 0 && !matches_any(config->include, config->includeCount, base))
        return 0;
    return !matches_any(config->exclude, config->excludeCount, base);
}

/* ---- the output ---------------------------------------------------------------------- */

typedef struct {
    unsigned char* data;
    size_t         capacity, length;
    int            ending;
    unsigned long long lineNo, offset;
} SavedLine;

typedef struct {
    Regex*       regex;
    RegexInput   input;
    int          prefixName;            /* print the file name in front of the lines */
    int          context;               /* any context is wanted */
    long         after, before;
    int          printedSomething;      /* a line was written, in this file or an earlier one */
    int          error;                 /* some file could not be read */
    int          matchedAny;            /* some line was selected */
    char         terminator;
} Search;

static void print_name(Config* config, const char* name, char separator, const Search* s) {
    (void)s;
    fputs(name, stdout);
    if (config->nullAfterName)
        fputc('\0', stdout);
    else
        fputc(separator, stdout);
}

static void print_prefix(Config* config, const Search* s, const char* name, unsigned long long lineNo,
                         unsigned long long offset, char separator) {
    if (s->prefixName)
        print_name(config, name, separator, s);
    if (config->lineNumber)
        printf("%llu%c", lineNo, separator);
    if (config->byteOffset)
        printf("%llu%c", offset, separator);
}

static void write_ending(int ending, const Search* s) {
    if (ending == 2 && s->terminator == '\n')
        fputc('\r', stdout);
    fputc(s->terminator, stdout);
}

static void print_line(Config* config, const Search* s, const char* name, const unsigned char* content, size_t length, int ending,
                       unsigned long long lineNo, unsigned long long offset, char separator) {
    print_prefix(config, s, name, lineNo, offset, separator);
    fwrite(content, 1, length, stdout);
    write_ending(ending, s);
}

/* ---- scanning one input --------------------------------------------------------------- */

typedef struct {
    SavedLine* ring;                    /* the last non-selected lines, for -B */
    long       ringSize, ringStart, ringCount;
    unsigned long long lastPrinted;     /* line number of the last line written for this file */
    long       afterLeft;               /* trailing context lines still to write */
} Context;

static void save_line(Context* c, const TextLine* line, unsigned long long lineNo, unsigned long long offset) {
    SavedLine* slot;

    if (c->ringSize == 0)
        return;
    if (c->ringCount == c->ringSize) {
        slot = &c->ring[c->ringStart];
        c->ringStart = (c->ringStart + 1) % c->ringSize;
        c->ringCount--;
    }
    slot = &c->ring[(c->ringStart + c->ringCount) % c->ringSize];
    c->ringCount++;

    if (VEC_RESERVE(slot->data, slot->capacity, line->length + 1) != 0)
        return;
    memcpy(slot->data, line->content, line->length);
    slot->length = line->length;
    slot->ending = line->ending;
    slot->lineNo = lineNo;
    slot->offset = offset;
}

/** "--" between groups that are not next to each other. */
static void print_separator(Search* s, const Context* c, unsigned long long firstLine) {
    if (s->context && s->printedSomething && (c->lastPrinted == 0 || firstLine != c->lastPrinted + 1)) {
        fputs("--", stdout);
        fputc('\n', stdout);
    }
}

static void flush_before(Config* config, Search* s, Context* c, const char* name) {
    long k;

    for (k = 0; k < c->ringCount; k++) {
        const SavedLine* slot = &c->ring[(c->ringStart + k) % c->ringSize];
        if (k == 0)
            print_separator(s, c, slot->lineNo);
        print_line(config, s, name, slot->data, slot->length, slot->ending, slot->lineNo, slot->offset, '-');
        c->lastPrinted = slot->lineNo;
        s->printedSomething = 1;
    }
    c->ringCount = 0;
}

/** -o: each non-empty match of the line on a line of its own. */
static void print_matches(Config* config, Search* s, const char* name, const TextLine* line, unsigned long long lineNo,
                          unsigned long long offset) {
    size_t from = 0, start, end;

    while (regex_search(s->regex, &s->input, from, &start, &end)) {
        if (end == start) {
            from = end + 1;
            if (from > s->input.length)
                break;
            continue;
        }
        print_prefix(config, s, name, lineNo, offset + s->input.offset[start], ':');
        fwrite(line->content + s->input.offset[start], 1, s->input.offset[end] - s->input.offset[start], stdout);
        write_ending(1, s);
        from = end;
    }
}

/** Whether the line is selected: it matches (or, with -v, does not). */
static int line_selected(Config* config, Search* s, const TextLine* line) {
    size_t start, end;

    if (regex_input_set(&s->input, line->content, line->length) != 0) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        exit(2);
    }
    return (regex_search(s->regex, &s->input, 0, &start, &end) != 0) != (config->invert != 0);
}

/** A regular file is looked at in its first block: a NUL there makes it a binary file. */
static int looks_binary(Config* config, FILE* in) {
    unsigned char block[32768];
    long where;
    size_t got, k;

    if (in == stdin || config->text || config->nullData)
        return 0;
    where = ftell(in);
    if (where < 0)
        return 0;
    got = fread(block, 1, sizeof block, in);
    fseek(in, where, SEEK_SET);
    for (k = 0; k < got; k++)
        if (block[k] == '\0')
            return 1;
    return 0;
}

/** Searches one open input. Returns how many lines were selected. */
/** The bytes a line takes in the file, with its ending. */
static unsigned long long line_bytes(const TextLine* line) {
    return line->length + (line->ending == 2 ? 2 : (line->ending == 1 ? 1 : 0));
}

/** Writes a selected line (with the context before it). Returns 1 when only "Binary file ... matches" was said. */
static int print_selected(Config* config, Search* s, Context* c, const char* name, const TextLine* line, unsigned long long lineNo,
                          unsigned long long offset, long after, int binary) {
    if (binary && !config->text) {
        printf("Binary file %s matches\n", name);
        s->printedSomething = 1;
        return 1;
    }
    flush_before(config, s, c, name);
    if (config->onlyMatching) {
        if (!config->invert)
            print_matches(config, s, name, line, lineNo, offset);
    } else {
        print_separator(s, c, lineNo);
        print_line(config, s, name, line->content, line->length, line->ending, lineNo, offset, ':');
    }
    c->lastPrinted = lineNo;
    s->printedSomething = 1;
    c->afterLeft = after;
    return 0;
}

/** After the last selected line (-m): the lines of trailing context that are still due. */
static void print_trailing_context(Config* config, Search* s, Context* c, LineReader* reader, const char* name,
                                   unsigned long long* lineNo, unsigned long long* offset) {
    TextLine line;
    while (c->afterLeft > 0 && text_reader_next(reader, &line)) {
        (*lineNo)++;
        print_line(config, s, name, line.content, line.length, line.ending, *lineNo, *offset, '-');
        *offset += line_bytes(&line);
        c->afterLeft--;
    }
}

static void free_context(Context* c) {
    long k;
    if (c->ring == NULL)
        return;
    for (k = 0; k < c->ringSize; k++)
        free(c->ring[k].data);
    free(c->ring);
}

static unsigned long long search_stream(Config* config, Search* s, FILE* in, const char* name) {
    LineReader reader;
    TextLine line;
    Context c;
    unsigned long long selected = 0, lineNo = 0, offset = 0;
    int binary = looks_binary(config, in);
    int quietOutput = config->count || config->listMatching || config->listNonMatching || config->quiet;
    long after = config->onlyMatching ? 0 : s->after;

    memset(&c, 0, sizeof c);
    c.ringSize = config->onlyMatching ? 0 : s->before;
    if (c.ringSize > 0)
        c.ring = calloc((size_t)c.ringSize, sizeof *c.ring);
    if (c.ringSize > 0 && c.ring == NULL)
        c.ringSize = 0;

    if (binary && config->withoutMatch)
        goto done;

    text_reader_init(&reader, in, s->terminator);

    while (text_reader_next(&reader, &line)) {
        unsigned long long thisOffset = offset;
        int isSelected;

        lineNo++;
        offset += line_bytes(&line);

        if (!binary && !config->text && !config->nullData && memchr(line.content, '\0', line.length) != NULL)
            binary = 1;
        if (binary && config->withoutMatch)
            break;

        isSelected = line_selected(config, s, &line);
        if (isSelected) {
            selected++;
            s->matchedAny = 1;
            if (config->quiet)
                break;
            if (config->listMatching || config->listNonMatching)
                break;
            if (!quietOutput && print_selected(config, s, &c, name, &line, lineNo, thisOffset, after, binary))
                break;
            if (config->maxCount >= 0 && selected >= (unsigned long long)config->maxCount) {
                if (!quietOutput)
                    print_trailing_context(config, s, &c, &reader, name, &lineNo, &offset);
                break;
            }
        } else if (!quietOutput) {
            if (c.afterLeft > 0) {
                print_line(config, s, name, line.content, line.length, line.ending, lineNo, thisOffset, '-');
                c.lastPrinted = lineNo;
                c.afterLeft--;
            } else {
                save_line(&c, &line, lineNo, thisOffset);
            }
        }

        // a pipe's data is passed on when the reader has nothing more of it for now
        if (reader.start >= reader.length)
            fflush(stdout);
    }

    if (reader.failed)
        s->error = 1;
    text_reader_free(&reader);

done:
    free_context(&c);
    return selected;
}

/** What is written about a file after it was searched: a count, or its name for -l and -L. */
static void report_file(Config* config, const Search* s, const char* name, unsigned long long selected) {
    if (config->quiet)
        return;
    if (config->count) {
        if (s->prefixName)
            print_name(config, name, ':', s);
        printf("%llu\n", selected);
    }
    if ((config->listMatching && selected > 0) || (config->listNonMatching && selected == 0)) {
        fputs(name, stdout);
        fputc(config->nullAfterName ? '\0' : '\n', stdout);
    }
}

static void report_open_error(Search* s) {
    s->error = 1;
}

static void search_file(Config* config, Search* s, const char* path, const char* display) {
    FILE* in;
    unsigned long long selected;

    if (strcmp(path, "-") == 0) {
        in = stdin;
        display = config->label != NULL ? config->label : "(standard input)";
    } else {
        char native[VPATH_MAX];
        DWORD attributes;

        if (vpath_to_native(path, native, sizeof native) == 0 && fs_attributes(native, &attributes) == 0 &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (!config->skipDirectories) {
                if (!config->noMessages)
                    fprintf(stderr, PROGRAM ": %s: Is a directory\n", display);
                s->error = 1;       // as GNU grep: the status is 2, whatever -s says
            }
            return;
        }
        if (config->noMessages) {
            // the message of text_open is not wanted: open it quietly
            if (vpath_to_native(path, native, sizeof native) != 0 || (in = fopen(native, "rb")) == NULL) {
                report_open_error(s);
                return;
            }
        } else if (text_open(PROGRAM, path, 1, &in) != 0) {
            report_open_error(s);
            return;
        }
    }

    selected = search_stream(config, s, in, display);
    if (in != stdin)
        fclose(in);
    else
        clearerr(stdin);
    report_file(config, s, display, selected);
}

/* ---- directories ---------------------------------------------------------------------- */

static int compare_names(const void* left, const void* right) {
    return strcmp(*(const char* const*)left, *(const char* const*)right);
}

static void search_path(Config* config, Search* s, const char* path, const char* display, int topLevel);

/** The files of a directory, in the order of their names. */
static void search_directory(Config* config, Search* s, const char* path, const char* display, const char* native) {
    char pattern[VPATH_MAX + 4];
    WIN32_FIND_DATAA data;
    HANDLE find;
    char** names = NULL;
    size_t count = 0, capacity = 0, i;

    snprintf(pattern, sizeof pattern, "%s\\*", native);
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        s->error = 1;
        return;
    }
    do {
        char* copy;
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
            continue;
        copy = _strdup(data.cFileName);
        if (copy == NULL || VEC_RESERVE(names, capacity, count + 1) != 0) {
            free(copy);
            break;
        }
        names[count++] = copy;
    } while (FindNextFileA(find, &data));
    FindClose(find);

    qsort(names, count, sizeof *names, compare_names);
    for (i = 0; i < count; i++) {
        char childPath[VPATH_MAX], childDisplay[VPATH_MAX];
        fs_join(path, names[i], childPath, sizeof childPath);
        if (display[0] == '\0')
            snprintf(childDisplay, sizeof childDisplay, "%s", names[i]);
        else
            fs_join(display, names[i], childDisplay, sizeof childDisplay);
        search_path(config, s, childPath, childDisplay, 0);
        free(names[i]);
    }
    free(names);
}

static void search_path(Config* config, Search* s, const char* path, const char* display, int topLevel) {
    char native[VPATH_MAX];
    DWORD attributes;

    if (config->quiet && s->matchedAny)
        return;

    if (strcmp(path, "-") != 0 && vpath_to_native(path, native, sizeof native) == 0 &&
        fs_attributes(native, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY) && config->recursive) {
        char base[VPATH_MAX];
        fs_basename(path, base, sizeof base);
        if (!topLevel && matches_any(config->excludeDir, config->excludeDirCount, base))
            return;
        search_directory(config, s, path, display, native);
        return;
    }

    if (!topLevel || config->includeCount > 0 || config->excludeCount > 0) {
        if (strcmp(path, "-") != 0 && !file_selected(config, path))
            return;
    }
    search_file(config, s, path, display);
}

/* ---- main ------------------------------------------------------------------------------ */

/** The flags of the regular expression engine that the options ask for. */
static int regex_flags(Config* config) {
    int flags = 0;

    if (config->extended)
        flags |= REGEX_EXTENDED;
    if (config->fixed)
        flags |= REGEX_FIXED;
    if (config->icase)
        flags |= REGEX_ICASE;
    if (config->line)
        flags |= REGEX_LINE;
    else if (config->word)
        flags |= REGEX_WORD;
    return flags;
}

/** -A, -B and -C: how many lines of context come after and before a selected line. */
static void set_context(Config* config, Search* search) {
    long context = config->context >= 0 ? config->context : 0;

    search->after = config->after >= 0 ? config->after : context;
    search->before = config->before >= 0 ? config->before : context;
    search->context = search->after > 0 || search->before > 0 || config->context >= 0 ||
                      config->after >= 0 || config->before >= 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int operandCount, result, i;
    char error[256];
    Search search;
    const char* defaultOperand = "-";
    Config settings;
    Config* config = &settings;

    memset(config, 0, sizeof *config);
    config->withFilename = -1;
    config->maxCount = -1;
    config->after = config->before = config->context = -1;

    result = args_parse(&spec, argc, argv, on_option, config, &operands, &operandCount);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    // the pattern: -e and -f, or else the first operand
    if (!config->patternGiven) {
        if (operandCount == 0) {
            fprintf(stderr, "Usage: " PROGRAM " [OPTION]... PATTERNS [FILE]...\n"
                            "Try '" PROGRAM " --help' for more information.\n");
            return 2;
        }
        if (add_pattern_text(config, operands[0]) != 0) {
            fprintf(stderr, PROGRAM ": memory exhausted\n");
            return 2;
        }
        operands++;
        operandCount--;
    }

    memset(&search, 0, sizeof search);
    search.regex = regex_compile(config->patterns, config->patternCount, regex_flags(config), error, sizeof error);
    if (search.regex == NULL) {
        fprintf(stderr, PROGRAM ": %s\n", error);
        return 2;
    }

    set_context(config, &search);
    search.terminator = config->nullData ? '\0' : '\n';

    text_binary_streams();

    if (config->recursive && operandCount == 0)
        defaultOperand = ".";

    search.prefixName = config->withFilename == 1 ||
                        (config->withFilename < 0 && (operandCount > 1 || config->recursive));

    if (config->maxCount == 0)
        return 1;

    if (operandCount == 0) {
        if (strcmp(defaultOperand, ".") == 0)
            search_path(config, &search, ".", "", 1);
        else
            search_path(config, &search, "-", "-", 1);
    }
    for (i = 0; i < operandCount; i++) {
        search_path(config, &search, operands[i], operands[i], 1);
        if (config->quiet && search.matchedAny)
            break;
    }

    if (fflush(stdout) != 0 && !search.error) {
        fprintf(stderr, PROGRAM ": write error\n");
        return 2;
    }
    if (config->quiet && search.matchedAny)
        return 0;
    if (search.error)
        return 2;
    return search.matchedAny ? 0 : 1;
}
