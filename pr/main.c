/*
 * pr: paginates or columnates files for printing, as GNU pr does. A page is a header of five lines (the date, the name of
 * the file or the -h text, and "Page N"), the body of 56 lines and a trailer of five lines; -COLUMN puts the text into
 * columns (down, or across with -a), -m prints the files side by side, -n numbers the lines, -o indents, -w/-W set the
 * width, -s/-S/-J choose the separators, -e/-i convert tabs, -c/-v show control characters, -d double spaces, -f/-F use
 * form feeds, -t/-T leave out the header, +FIRST[:LAST] chooses the pages. Columns are padded with tabs the way pr does it:
 * white space is kept back until something follows it and is then written as tabs where it can be.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include "args.h"
#include "datetime.h"
#include "textutil.h"
#include "vpath.h"
#include "fsutil.h"

#define VERSION "0.1"
#define PROGRAM "pr"

static void print_usage(void) {
    puts("Usage: pr [OPTION]... [FILE]...\n"
         "Paginate or columnate FILE(s) for printing.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  +FIRST_PAGE[:LAST_PAGE], --pages=FIRST_PAGE[:LAST_PAGE]\n"
         "                    begin [stop] printing with page FIRST_[LAST_]PAGE\n"
         "  -COLUMN, --columns=COLUMN\n"
         "                    output COLUMN columns and print columns down,\n"
         "                    unless -a is used. Balance number of lines in the\n"
         "                    columns on each page\n"
         "  -a, --across      print columns across rather than down, used together\n"
         "                    with -COLUMN\n"
         "  -c, --show-control-chars\n"
         "                    use hat notation (^G) and octal backslash notation\n"
         "  -d, --double-space\n"
         "                    double space the output\n"
         "  -D, --date-format=FORMAT\n"
         "                    use FORMAT for the header date\n"
         "  -e[CHAR[WIDTH]], --expand-tabs[=CHAR[WIDTH]]\n"
         "                    expand input CHARs (TABs) to tab WIDTH (8)\n"
         "  -F, -f, --form-feed\n"
         "                    use form feeds instead of newlines to separate pages\n"
         "                    (by a 3-line page header with -F or a 5-line header\n"
         "                    and trailer without -F)\n"
         "  -h, --header=HEADER\n"
         "                    use a centered HEADER instead of filename in page header,\n"
         "                    -h \"\" prints a blank line, don't use -h\"\"\n"
         "  -i[CHAR[WIDTH]], --output-tabs[=CHAR[WIDTH]]\n"
         "                    replace spaces with CHARs (TABs) to tab WIDTH (8)\n"
         "  -J, --join-lines  merge full lines, turns off -W line truncation, no column\n"
         "                    alignment, --sep-string[=STRING] sets separators\n"
         "  -l, --length=PAGE_LENGTH\n"
         "                    set the page length to PAGE_LENGTH (66) lines\n"
         "                    (default number of lines of text 56, and with -F 63).\n"
         "                    implies -t if PAGE_LENGTH <= 10\n"
         "  -m, --merge       print all files in parallel, one in each column,\n"
         "                    truncate lines, but join lines of full length with -J\n"
         "  -n[SEP[DIGITS]], --number-lines[=SEP[DIGITS]]\n"
         "                    number lines, use DIGITS (5) digits, then SEP (TAB),\n"
         "                    default counting starts with 1st line of input file\n"
         "  -N, --first-line-number=NUMBER\n"
         "                    start counting with NUMBER at 1st line of first\n"
         "                    page printed (see +FIRST_PAGE)\n"
         "  -o, --indent=MARGIN\n"
         "                    offset each line with MARGIN (zero) spaces, do not\n"
         "                    affect -w or -W, MARGIN will be added to PAGE_WIDTH\n"
         "  -r, --no-file-warnings\n"
         "                    omit warning when a file cannot be opened\n"
         "  -s[CHAR], --separator[=CHAR]\n"
         "                    separate columns by a single character, default for CHAR\n"
         "                    is the <TAB> character without -w and 'no char' with -w.\n"
         "                    -s[CHAR] turns off line truncation of all 3 column\n"
         "                    options (-COLUMN|-a -COLUMN|-m) except -w is set\n"
         "  -S[STRING], --sep-string[=STRING]\n"
         "                    separate columns by STRING,\n"
         "                    without -S: Default separator <TAB> with -J and <space>\n"
         "                    otherwise (same as -S\" \"), no effect on column options\n"
         "  -t, --omit-header  omit page headers and trailers;\n"
         "                     implied if PAGE_LENGTH <= 10\n"
         "  -T, --omit-pagination\n"
         "                    omit page headers and trailers, eliminate any pagination\n"
         "                    by form feeds set in input files\n"
         "  -v, --show-nonprinting\n"
         "                    use octal backslash notation\n"
         "  -w, --width=PAGE_WIDTH\n"
         "                    set page width to PAGE_WIDTH (72) characters for\n"
         "                    multiple text-column output only, -s[char] turns off (72)\n"
         "  -W, --page-width=PAGE_WIDTH\n"
         "                    set page width to PAGE_WIDTH (72) characters always,\n"
         "                    truncate lines, except -J option is set, no interference\n"
         "                    with -S or -s\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit");
}

enum {
    OPT_PAGES = 1, OPT_COLUMNS, OPT_ACROSS, OPT_CONTROL, OPT_DOUBLE, OPT_DATE, OPT_EXPAND, OPT_FORM, OPT_HEADER, OPT_OUTTABS,
    OPT_JOIN, OPT_LENGTH, OPT_MERGE, OPT_NUMBER, OPT_FIRST, OPT_INDENT, OPT_NOWARN, OPT_SEPARATOR, OPT_SEPSTRING, OPT_OMIT,
    OPT_NOPAGE, OPT_NONPRINT, OPT_WIDTH, OPT_PAGEWIDTH, OPT_BALANCE
};

static const ArgsOption options[] = {
    { OPT_PAGES,     0,   "pages",                ARGS_REQUIRED },
    { OPT_COLUMNS,   0,   "columns",              ARGS_REQUIRED },
    { OPT_ACROSS,    'a', "across",               ARGS_NONE },
    { OPT_BALANCE,   'b', NULL,                   ARGS_NONE },
    { OPT_CONTROL,   'c', "show-control-chars",   ARGS_NONE },
    { OPT_DOUBLE,    'd', "double-space",         ARGS_NONE },
    { OPT_DATE,      'D', "date-format",          ARGS_REQUIRED },
    { OPT_EXPAND,    'e', "expand-tabs",          ARGS_OPTIONAL },
    { OPT_FORM,      'f', "form-feed",            ARGS_NONE },
    { OPT_FORM,      'F', NULL,                   ARGS_NONE },
    { OPT_HEADER,    'h', "header",               ARGS_REQUIRED },
    { OPT_OUTTABS,   'i', "output-tabs",          ARGS_OPTIONAL },
    { OPT_JOIN,      'J', "join-lines",           ARGS_NONE },
    { OPT_LENGTH,    'l', "length",               ARGS_REQUIRED },
    { OPT_MERGE,     'm', "merge",                ARGS_NONE },
    { OPT_NUMBER,    'n', "number-lines",         ARGS_OPTIONAL },
    { OPT_FIRST,     'N', "first-line-number",    ARGS_REQUIRED },
    { OPT_INDENT,    'o', "indent",               ARGS_REQUIRED },
    { OPT_NOWARN,    'r', "no-file-warnings",     ARGS_NONE },
    { OPT_SEPARATOR, 's', "separator",            ARGS_OPTIONAL },
    { OPT_SEPSTRING, 'S', "sep-string",           ARGS_OPTIONAL },
    { OPT_OMIT,      't', "omit-header",          ARGS_NONE },
    { OPT_NOPAGE,    'T', "omit-pagination",      ARGS_NONE },
    { OPT_NONPRINT,  'v', "show-nonprinting",     ARGS_NONE },
    { OPT_WIDTH,     'w', "width",                ARGS_REQUIRED },
    { OPT_PAGEWIDTH, 'W', "page-width",           ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 1 };

/* ---- the settings and the state of the output ---- */

enum { OPEN = 0, FF_FOUND = 1, CLOSED = 2 };

#define CHARS_PER_LINE 72
#define HEADER_LINES 5
#define FOOTER_LINES 5

typedef struct {
    unsigned char* bytes;
    size_t         length, capacity;
    int*           tabs;            /* the columns each tab character of the text takes */
    size_t         tabCount, tabCapacity;
    int            dropped;         /* the line was cut away completely (nothing of it fits the column) */
} Cell;

/** One cell of a page: the text of a line (or nothing) and the number the line has. */
typedef struct {
    int       present;
    int       blankAfter;       /* row only (the first slot): a blank line follows when double spacing */
    int       abortRow;         /* row only: the row ends after the first cell, without a line feed (see fill_parallel) */
    long long number;
    Cell      cell;
} Slot;

typedef struct {
    unsigned char* data;
    size_t         length, pos;
    int            status;
    int            eof;             /* the end of the data was seen while reading */
    const char*    name;            /* as written; NULL for standard input */
    Instant        modified;
} Input;

typedef struct {
    /* the options */
    int  columns, columnsSet;
    int  across, parallel, doubleSpace, omitHeader, omitPagination, useFormFeed, joinLines;
    int  numbered, numberChars;
    char numberSep;
    int  showControl, showNonprinting;
    int  untabify, tabifyOption;
    char inTabChar, outTabChar;
    int  inTabWidth, outTabWidth;
    int  pageLength, margin;
    int  wGiven, widthW, pageWidthGiven, widthBig;
    int  sGiven, SGiven;
    const char* sText;
    const char* SText;
    const char* header;
    const char* dateFormat;
    long long firstPage, lastPage;
    int  startLine, startLineSet;
    int  lastWasNumber, forceTruncate, sHasArg;
    int  continueLine;                  /* the last row ended without a line feed: the next one goes on in that line */
    char digits[32];
    int  noWarnings;
    /* derived */
    int  extremities, keepFF;
    int  linesPerBody;                  /* the rows of a page */
    int  truncateLines, aligned, tabify, multi, accurateTabs;
    const char* colSep;
    int  colSepLen, charsPerColumn, numberWidth, charsPerLine;
    /* the state of the output */
    int  outPos, spaces, seps;
    int  quiet;
    long long lineNumber;
    unsigned long long pageNumber;
} Pr;

static void emit(Pr* p, int c) {
    if (!p->quiet)
        putc(c, stdout);
}

static int is_print(unsigned char c) {
    return c >= 0x20 && c < 0x7f;
}

/* ---- white space, padding and separators ---- */

#define TAB_WIDTH(w, pos) ((w) - ((pos) % (w)))

static void print_white_space(Pr* p) {
    int hNew;
    int hOld = p->outPos;
    int goal = hOld + p->spaces;

    while (goal - hOld > 1 && 0 < (hNew = hOld + TAB_WIDTH(p->outTabWidth, hOld))) {
        if (hNew > goal)
            break;
        emit(p, (unsigned char)p->outTabChar);
        hOld = hNew;
    }
    while (++hOld <= goal)
        emit(p, ' ');
    p->outPos = goal;
    p->spaces = 0;
}

static void print_char(Pr* p, unsigned char c, int tabAdvance) {
    if (p->tabify) {
        if (c == ' ') {
            p->spaces++;
            return;
        }
        if (p->spaces > 0)
            print_white_space(p);
        if (!is_print(c)) {
            if (c == '\b')
                p->outPos--;
            else if (p->accurateTabs && (c == '\t' || c == (unsigned char)p->inTabChar))
                p->outPos += tabAdvance;
        } else {
            p->outPos++;
        }
    }
    emit(p, c);
}

static void pad_across_to(Pr* p, int position) {
    int h = p->outPos;

    if (p->tabify) {
        p->spaces = position - p->outPos;
    } else {
        while (++h <= position)
            emit(p, ' ');
        p->outPos = position;
    }
}

static void print_sep_string(Pr* p) {
    for (; p->seps > 0; --p->seps) {
        const char* s = p->colSep;
        int l = p->colSepLen;

        while (l-- > 0) {
            if (*s == ' ') {
                p->spaces++;
                s++;
            } else {
                if (p->spaces > 0)
                    print_white_space(p);
                if (*s == '\t')
                    p->outPos += 1 + (p->accurateTabs ? TAB_WIDTH(8, p->outPos) : 0);
                else
                    p->outPos++;
                emit(p, (unsigned char)*s++);
            }
        }
    }
    if (p->spaces > 0)
        print_white_space(p);
}

static void add_line_number(Pr* p, long long number) {
    char buffer[64];
    int savedAccurate = p->accurateTabs;
    int length, i;
    const char* s;

    snprintf(buffer, sizeof buffer, "%*d", p->numberChars, (int)number);
    length = (int)strlen(buffer);
    s = buffer;
    if (length > p->numberChars)
        s += length - p->numberChars;           /* the high order digits are cut off */
    for (i = 0; i < p->numberChars && *s != '\0'; i++)
        print_char(p, (unsigned char)*s++, 0);
    p->accurateTabs = 1;
    if (p->multi) {
        if (p->numberSep == '\t') {
            for (i = p->numberWidth - p->numberChars; i > 0; i--)
                print_char(p, ' ', 0);
        } else {
            print_char(p, (unsigned char)p->numberSep, 0);
        }
    } else {
        print_char(p, (unsigned char)p->numberSep, TAB_WIDTH(p->outTabWidth, p->outPos));
    }
    p->accurateTabs = savedAccurate;
}

/* ---- reading ---- */

static void cell_put(Cell* cell, unsigned char c) {
    if (cell->length == cell->capacity) {
        cell->capacity = cell->capacity ? cell->capacity * 2 : 64;
        cell->bytes = (unsigned char*)realloc(cell->bytes, cell->capacity);
        if (cell->bytes == NULL) {
            fprintf(stderr, "%s: memory exhausted\n", PROGRAM);
            exit(1);
        }
    }
    cell->bytes[cell->length++] = c;
}

static void cell_put_tab(Cell* cell, unsigned char c, int width) {
    if (cell->tabCount == cell->tabCapacity) {
        cell->tabCapacity = cell->tabCapacity ? cell->tabCapacity * 2 : 8;
        cell->tabs = (int*)realloc(cell->tabs, sizeof(int) * cell->tabCapacity);
        if (cell->tabs == NULL) {
            fprintf(stderr, "%s: memory exhausted\n", PROGRAM);
            exit(1);
        }
    }
    cell->tabs[cell->tabCount++] = width;
    cell_put(cell, c);
}

/** The characters a character is shown as, and the columns it takes: pr's char_to_clump. */
static void add_clump(Pr* p, Cell* cell, unsigned char c, int* inputPosition) {
    int width, i, perC = 8;
    char esc[8];

    if (c == (unsigned char)p->inTabChar)
        perC = p->inTabWidth;
    if (c == (unsigned char)p->inTabChar || c == '\t') {
        width = TAB_WIDTH(perC, *inputPosition);
        if (p->untabify) {
            for (i = width; i > 0; i--)
                cell_put(cell, ' ');
        } else {
            cell_put_tab(cell, c, width);
        }
    } else if (!is_print(c)) {
        if (p->showNonprinting) {
            width = 4;
            snprintf(esc, sizeof esc, "%03o", c);
            cell_put(cell, '\\');
            for (i = 0; i <= 2; i++)
                cell_put(cell, (unsigned char)esc[i]);
        } else if (p->showControl) {
            if (c < 0200) {
                width = 2;
                cell_put(cell, '^');
                cell_put(cell, (unsigned char)(c ^ 0100));
            } else {
                width = 4;
                snprintf(esc, sizeof esc, "%03o", c);
                cell_put(cell, '\\');
                for (i = 0; i <= 2; i++)
                    cell_put(cell, (unsigned char)esc[i]);
            }
        } else {
            width = 0;
            cell_put(cell, c);
        }
    } else {
        width = 1;
        cell_put(cell, c);
    }
    *inputPosition += width;
}

static void eat_newline(Input* in) {
    if (in->pos < in->length && in->data[in->pos] == '\n')
        in->pos++;
}

/**
 * Reads the next line of a file into the cell. A form feed ends the page: at the start of a line it makes no line, inside
 * a line it ends it, and the line feed after it is swallowed. Returns 1 when there is a line.
 */
static int read_cell(Pr* p, Input* in, Cell* cell, int limit, int startPosition) {
    int inputPosition = startPosition, last = startPosition;
    unsigned char c;

    cell->length = 0;
    cell->tabCount = 0;
    cell->dropped = 0;
    if (in->pos >= in->length) {
        in->status = CLOSED;
        in->eof = 1;
        return 0;
    }
    c = in->data[in->pos++];
    if (c == '\f') {
        in->status = FF_FOUND;
        eat_newline(in);
        return 0;
    }
    for (;;) {
        if (c == '\n')
            break;
        if (c == '\f') {
            in->status = FF_FOUND;
            eat_newline(in);
            break;
        }
        {
            size_t before = cell->length, tabsBefore = cell->tabCount;

            add_clump(p, cell, c, &inputPosition);
            if (p->truncateLines && inputPosition > limit) {
                cell->length = before;
                cell->tabCount = tabsBefore;
                if (cell->length == 0 && !p->accurateTabs)
                    cell->dropped = 1;
                inputPosition = last;
                while (in->pos < in->length && in->data[in->pos] != '\n')
                    in->pos++;
                if (in->pos < in->length)
                    in->pos++;
                return 1;
            }
            last = inputPosition;
        }
        if (in->pos >= in->length) {
            in->eof = 1;
            break;
        }
        c = in->data[in->pos++];
    }
    return 1;
}

static int has_data(const Input* in) {
    return in->pos < in->length;
}

/* ---- a page ---- */

typedef struct {
    Slot* slots;
    int   rowsUsed, rowsAllocated, columns;
    int   full;                 /* the page was filled to its end */
} Page;

static Slot* page_row(Page* page, int row) {
    if (row >= page->rowsAllocated) {
        int n = page->rowsAllocated ? page->rowsAllocated * 2 : 64;
        int i;

        while (n <= row)
            n *= 2;
        page->slots = (Slot*)realloc(page->slots, sizeof(Slot) * (size_t)n * (size_t)page->columns);
        if (page->slots == NULL) {
            fprintf(stderr, "%s: memory exhausted\n", PROGRAM);
            exit(1);
        }
        memset(page->slots + (size_t)page->rowsAllocated * page->columns, 0,
               sizeof(Slot) * (size_t)(n - page->rowsAllocated) * (size_t)page->columns);
        (void)i;
        page->rowsAllocated = n;
    }
    return page->slots + (size_t)row * page->columns;
}

static int text_limit(const Pr* p, int numberedCell) {
    return p->charsPerColumn + (numberedCell && p->parallel ? p->numberWidth : 0);
}

/** Where the text of a cell starts: after the line number. */
static int text_start(const Pr* p, int numberedCell) {
    return numberedCell && p->truncateLines ? p->numberWidth : 0;
}

static void clear_row(Slot* row, int columns) {
    int j;

    for (j = 0; j < columns; j++) {
        row[j].present = 0;
        row[j].cell.length = 0;
    }
}

/** Reads one page of a single file printed in one column. */
static void fill_single(Pr* p, Input* in, Page* page) {
    int r;

    for (r = 0; r < p->linesPerBody; r++) {
        Slot* row = page_row(page, r);

        clear_row(row, page->columns);
        if (in->status != OPEN || !has_data(in))
            break;
        row[0].present = read_cell(p, in, &row[0].cell, text_limit(p, p->numbered), text_start(p, p->numbered));
        if (!row[0].present)
            break;
        row[0].number = p->lineNumber++;
        page->rowsUsed = r + 1;
        row[0].blankAfter = !in->eof && in->status == OPEN;
        if (in->status != OPEN)
            break;
    }
}

/** Reads one page of a single file printed across columns. */
static void fill_across(Pr* p, Input* in, Page* page) {
    int r, j;

    for (r = 0; r < p->linesPerBody; r++) {
        Slot* row = page_row(page, r);
        int any = 0;

        clear_row(row, page->columns);
        for (j = 0; j < p->columns; j++) {
            if (in->status != OPEN || !has_data(in)) {
                if (in->status == OPEN)
                    in->eof = 1;
                break;
            }
            row[j].present = read_cell(p, in, &row[j].cell, text_limit(p, p->numbered), text_start(p, p->numbered));
            if (!row[j].present)
                break;
            row[j].number = p->lineNumber++;
            any = 1;
        }
        if (!any)
            break;
        page->rowsUsed = r + 1;
        row[0].blankAfter = !in->eof && in->status == OPEN;
        if (in->status != OPEN)
            break;
    }
}

/** Reads one page of a single file printed down columns: the lines are stored and then shared out evenly. */
static void fill_down(Pr* p, Input* in, Page* page) {
    long long capacity = (long long)p->linesPerBody * p->columns;
    Slot* store = NULL;
    long long stored = 0, storeCap = 0;
    int j, r;
    long long first = 0;

    while (stored < capacity && in->status == OPEN && has_data(in)) {
        Slot* s;

        if (stored == storeCap) {
            long long n = storeCap ? storeCap * 2 : 256;

            store = (Slot*)realloc(store, sizeof(Slot) * (size_t)n);
            memset(store + storeCap, 0, sizeof(Slot) * (size_t)(n - storeCap));
            storeCap = n;
        }
        s = &store[stored];
        s->present = read_cell(p, in, &s->cell, text_limit(p, p->numbered), text_start(p, p->numbered));
        if (!s->present)
            break;
        s->number = p->lineNumber++;
        stored++;
    }
    if (stored < capacity && in->status == OPEN && !has_data(in))
        in->eof = 1;
    page->full = stored >= capacity;
    for (j = 0; j < p->columns; j++) {
        long long lines = stored / p->columns + (j < stored % p->columns ? 1 : 0);

        for (r = 0; r < lines; r++) {
            Slot* row = page_row(page, r);
            Slot* from = &store[first + r];

            if (j == 0)
                clear_row(row, page->columns);
            row[j].present = 1;
            row[j].number = from->number;
            row[j].cell.length = 0;
            row[j].cell.tabCount = 0;
            row[j].cell.dropped = from->cell.dropped;
            {
                size_t k;

                for (k = 0; k < from->cell.length; k++)
                    cell_put(&row[j].cell, from->cell.bytes[k]);
                for (k = 0; k < from->cell.tabCount; k++) {
                    if (row[j].cell.tabCount == row[j].cell.tabCapacity) {
                        row[j].cell.tabCapacity = row[j].cell.tabCapacity ? row[j].cell.tabCapacity * 2 : 8;
                        row[j].cell.tabs = (int*)realloc(row[j].cell.tabs, sizeof(int) * row[j].cell.tabCapacity);
                    }
                    row[j].cell.tabs[row[j].cell.tabCount++] = from->cell.tabs[k];
                }
            }
            if (r + 1 > page->rowsUsed)
                page->rowsUsed = r + 1;
        }
        first += lines;
    }
    for (r = 0; r < page->rowsUsed; r++)
        page_row(page, r)[0].blankAfter = r < page->rowsUsed - 1 || (!in->eof && in->status == OPEN);
    {
        long long i;

        for (i = 0; i < storeCap; i++)
            free(store[i].cell.bytes);
        free(store);
    }
}

/** Reads one page of the files that are printed side by side. */
static void fill_parallel(Pr* p, Input* files, int count, Page* page) {
    int r, j;

    for (r = 0; r < p->linesPerBody; r++) {
        Slot* row = page_row(page, r);
        int open = 0, any = 0;

        clear_row(row, page->columns);
        row[0].abortRow = 0;
        for (j = 0; j < count; j++) {
            if (files[j].status == OPEN && has_data(&files[j]))
                open = 1;
        }
        if (!open)
            break;
        for (j = 0; j < count; j++) {
            if (files[j].status == OPEN && !has_data(&files[j]))
                files[j].eof = 1;
            if (files[j].status != OPEN || !has_data(&files[j]))
                continue;
            row[j].present = read_cell(p, &files[j], &row[j].cell, text_limit(p, p->numbered && j == 0), text_start(p, p->numbered && j == 0));
            if (row[j].present)
                any = 1;
            /* a numbered first file whose last line has no line feed: pr ends the row there and goes on in the same line */
            if (j == 0 && p->numbered && count > 1 && row[0].present && files[0].eof) {
                int k, others = 0;

                for (k = 1; k < count; k++) {
                    if (has_data(&files[k]))
                        others = 1;
                }
                if (others) {
                    row[0].abortRow = 1;
                    break;
                }
            }
        }
        if (!any)
            break;
        row[0].number = p->lineNumber++;
        page->rowsUsed = r + 1;
        row[0].blankAfter = 0;
        for (j = 0; j < count; j++) {
            if (!files[j].eof && files[j].status == OPEN)
                row[0].blankAfter = 1;
        }
    }
}

/* ---- the output of a page ---- */

static void format_date(const Pr* p, Instant t, char* out, size_t size) {
    struct tm tm;
    const char* f = p->dateFormat;
    size_t used = 0;

    out[0] = '\0';
    if (datetime_localtime(t.seconds, &tm) != 0)
        return;
    while (*f != '\0' && used + 1 < size) {
        char piece[128];
        char spec[3] = { '%', 0, 0 };
        size_t n;

        if (*f != '%' || f[1] == '\0') {
            out[used++] = *f++;
            out[used] = '\0';
            continue;
        }
        f++;
        piece[0] = '\0';
        switch (*f) {
        case 'e': snprintf(piece, sizeof piece, "%2d", tm.tm_mday); break;
        case 'k': snprintf(piece, sizeof piece, "%2d", tm.tm_hour); break;
        case 'l': snprintf(piece, sizeof piece, "%2d", tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12); break;
        case 'P': snprintf(piece, sizeof piece, "%s", tm.tm_hour < 12 ? "am" : "pm"); break;
        case 's': snprintf(piece, sizeof piece, "%lld", (long long)t.seconds); break;
        case 'n': snprintf(piece, sizeof piece, "\n"); break;
        case 't': snprintf(piece, sizeof piece, "\t"); break;
        case 'N': snprintf(piece, sizeof piece, "%09ld", t.nanos); break;
        case 'F': strftime(piece, sizeof piece, "%Y-%m-%d", &tm); break;
        case 'T': strftime(piece, sizeof piece, "%H:%M:%S", &tm); break;
        case 'R': strftime(piece, sizeof piece, "%H:%M", &tm); break;
        case 'D': strftime(piece, sizeof piece, "%m/%d/%y", &tm); break;
        case 'h': strftime(piece, sizeof piece, "%b", &tm); break;
        case 'r': strftime(piece, sizeof piece, "%I:%M:%S %p", &tm); break;
        case 'z': {
            long offset = 0;
            struct tm gm;
            __int64 local, utc;

            if (datetime_gmtime(t.seconds, &gm) == 0) {
                local = datetime_gm_seconds(&tm);
                utc = t.seconds;
                offset = (long)((local - utc) / 60);
            }
            snprintf(piece, sizeof piece, "%c%02ld%02ld", offset < 0 ? '-' : '+', labs(offset) / 60, labs(offset) % 60);
            break;
        }
        default:
            spec[1] = *f;
            if (strftime(piece, sizeof piece, spec, &tm) == 0)
                snprintf(piece, sizeof piece, "%%%c", *f);
            break;
        }
        f++;
        n = strlen(piece);
        if (used + n >= size)
            n = size - 1 - used;
        memcpy(out + used, piece, n);
        used += n;
        out[used] = '\0';
    }
}

static void print_header(Pr* p, const char* dateText, const char* fileText) {
    char pageText[64];
    int available, lhs, rhs, i;

    p->outPos = 0;
    pad_across_to(p, p->margin);
    print_white_space(p);
    snprintf(pageText, sizeof pageText, "Page %llu", p->pageNumber);
    available = p->charsPerLine - (int)strlen(dateText) - (int)strlen(fileText) - (int)strlen(pageText);
    if (available < 0)
        available = 0;
    lhs = available >> 1;
    rhs = available - lhs;
    emit(p, '\n');
    emit(p, '\n');
    for (i = 0; i < p->margin; i++)
        emit(p, ' ');
    for (i = 0; dateText[i] != '\0'; i++)
        emit(p, (unsigned char)dateText[i]);
    for (i = 0; i < (lhs > 1 ? lhs : 1); i++)
        emit(p, ' ');
    for (i = 0; fileText[i] != '\0'; i++)
        emit(p, (unsigned char)fileText[i]);
    for (i = 0; i < (rhs > 1 ? rhs : 1); i++)
        emit(p, ' ');
    for (i = 0; pageText[i] != '\0'; i++)
        emit(p, (unsigned char)pageText[i]);
    emit(p, '\n');
    emit(p, '\n');
    emit(p, '\n');
    p->outPos = 0;
}

/** The start of the column j: where its text begins (the margin counts). */
static int column_start(const Pr* p, int j) {
    int start = p->margin + j * (p->charsPerColumn + p->colSepLen);

    if (p->parallel && p->numbered && j > 0)
        start += p->numberWidth;
    return start;
}

static void print_row(Pr* p, const Slot* row, int columns, int blankAfter) {
    int j;
    int owed = 0;

    if (!p->continueLine)
        p->outPos = 0;
    p->continueLine = row[0].abortRow;
    p->spaces = 0;
    p->seps = 0;
    if (p->margin > 0) {
        pad_across_to(p, p->margin);
        if (p->multi)
            print_white_space(p);
    }
    for (j = 0; j < columns; j++) {
        const Slot* slot = &row[j];
        size_t k;

        if (!slot->present && !p->parallel)
            break;
        if (!slot->present && !row[0].blankAfter)
            break;                  /* every file is at its end: no padding for the empty cells */
        if (j > 0 && row[0].abortRow)
            break;
        if (slot->present && slot->cell.dropped) {
            owed++;
            continue;
        }
        if (j > 0) {
            if (p->aligned && column_start(p, j) - p->colSepLen > 0)
                pad_across_to(p, column_start(p, j) - p->colSepLen);
            p->seps = 1 + owed;
            owed = 0;
            print_sep_string(p);
        }
        if (p->numbered && (!p->parallel || j == 0))
            add_line_number(p, slot->number);
        {
            size_t t = 0;

            for (k = 0; k < slot->cell.length; k++) {
                unsigned char c = slot->cell.bytes[k];
                int advance = 0;

                if (!p->untabify && (c == '\t' || c == (unsigned char)p->inTabChar) && t < slot->cell.tabCount)
                    advance = slot->cell.tabs[t++];
                print_char(p, c, advance);
            }
        }
    }
    if (row[0].abortRow)
        return;
    emit(p, '\n');
    if (p->doubleSpace && blankAfter)
        emit(p, '\n');
}

static void print_page(Pr* p, Page* page, int ffFound, const char* dateText, const char* fileText) {
    int r, padding, printed = 0;

    if (p->extremities)
        print_header(p, dateText, fileText);
    for (r = 0; r < page->rowsUsed; r++) {
        const Slot* row = page->slots + (size_t)r * page->columns;
        int blank = p->doubleSpace && row[0].blankAfter;

        print_row(p, row, page->columns, blank);
        printed += 1 + (blank ? 1 : 0);
    }
    if (p->extremities) {
        if (p->useFormFeed) {
            if (page->rowsUsed == 0 || (p->doubleSpace && printed < p->linesPerBody * 2))
                emit(p, '\n');
            emit(p, '\f');
        } else {
            padding = p->linesPerBody * (p->doubleSpace ? 2 : 1) - printed;
            while (padding-- > 0)
                emit(p, '\n');
            for (r = 0; r < FOOTER_LINES; r++)
                emit(p, '\n');
        }
    } else if (ffFound && p->keepFF) {
        emit(p, '\f');
    }
}

static void free_page(Page* page) {
    int i, j;

    for (i = 0; i < page->rowsAllocated; i++) {
        for (j = 0; j < page->columns; j++)
            free(page->slots[(size_t)i * page->columns + j].cell.bytes);
    }
    free(page->slots);
}

/* ---- the files ---- */

/** A file name for a message: in quotes when the shell would need them (like quotef of the GNU tools). */
static const char* quote_file(const char* name, char* out, size_t size) {
    const char* special = " \t\n!\"#$&'()*;<>?[\\]^`{|}~:";
    size_t used = 0;
    const char* q;

    if (name[0] != '\0' && strpbrk(name, special) == NULL)
        return name;
    out[used++] = '\'';
    for (q = name; *q != '\0' && used + 6 < size; q++) {
        if (*q == '\'') {
            memcpy(out + used, "\'\\\''", 4);
            used += 4;
        } else {
            out[used++] = *q;
        }
    }
    out[used++] = '\'';
    out[used] = '\0';
    return out;
}

static int open_input(Pr* p, const char* name, Input* in) {
    FILE* file;
    char native[VPATH_MAX];
    int kind;
    const char* reason = NULL;
    char quoted[VPATH_MAX + 8];

    memset(in, 0, sizeof *in);
    in->name = strcmp(name, "-") == 0 ? NULL : name;
    if (in->name == NULL) {
        file = stdin;
        in->modified = datetime_now();
    } else {
        DWORD attributes;
        WIN32_FILE_ATTRIBUTE_DATA info;

        kind = vpath_to_native(name, native, sizeof native);
        if (kind == VPATH_ROOT) {
            reason = "Is a directory";
        } else if (kind != 0) {
            reason = strerror(errno);
        } else if (fs_attributes(native, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            reason = "Is a directory";
        }
        file = NULL;
        if (reason == NULL) {
            file = fopen(native, "rb");
            if (file == NULL) {
                reason = errno == ENOENT ? "No such file or directory" : fs_strerror(GetLastError());
            } else if (GetFileAttributesExA(native, GetFileExInfoStandard, &info)) {
                in->modified = datetime_from_filetime(info.ftLastWriteTime);
            }
        }
        if (file == NULL) {
            if (!p->noWarnings)
                fprintf(stderr, "%s: %s: %s\n", PROGRAM, quote_file(name, quoted, sizeof quoted), reason);
            return -1;
        }
    }
    in->data = text_slurp(file, &in->length);
    if (file != stdin)
        fclose(file);
    if (in->data == NULL)
        in->data = (unsigned char*)malloc(1);
    return 0;
}

static void run_pages(Pr* p, Input* files, int count, const char* dateText, const char* fileText) {
    Page page;
    int j;
    int printedAny = 0;
    unsigned long long skipped = 0;
    int prevFull = 0, lastFull = 0;

    memset(&page, 0, sizeof page);
    page.columns = p->parallel ? count : p->columns;
    p->pageNumber = 0;
    p->lineNumber = 1;
    for (;;) {
        int more = 0, ff = 0;

        for (j = 0; j < count; j++) {
            if (files[j].status == FF_FOUND)
                files[j].status = OPEN;
            /* a form feed right after a page that was filled to its end is that page's end, not a page of its own */
            if (prevFull && files[j].pos < files[j].length && files[j].data[files[j].pos] == '\f') {
                files[j].pos++;
                eat_newline(&files[j]);
            }
            if (has_data(&files[j]))
                more = 1;
        }
        if (!more) {
            /* skipping to the first page, the attempt on the next page finds the end of the data */
            if (!printedAny && (long long)p->pageNumber + 1 < p->firstPage)
                fprintf(stderr, "%s: starting page number %lld exceeds page count %llu\n", PROGRAM, p->firstPage, p->pageNumber + 1);
            break;
        }
        p->pageNumber++;
        if (p->lastPage > 0 && (long long)p->pageNumber > p->lastPage)
            break;
        p->quiet = (long long)p->pageNumber < p->firstPage;
        if (!p->quiet && !printedAny) {
            printedAny = 1;
            if (p->startLineSet)
                p->lineNumber = p->startLine;
        }
        page.rowsUsed = 0;
        page.full = 0;
        if (p->parallel)
            fill_parallel(p, files, count, &page);
        else if (p->columns > 1 && p->across)
            fill_across(p, &files[0], &page);
        else if (p->columns > 1)
            fill_down(p, &files[0], &page);
        else
            fill_single(p, &files[0], &page);
        if (!(p->columns > 1 && !p->across && !p->parallel))
            page.full = page.rowsUsed >= p->linesPerBody;
        lastFull = page.full;
        if (p->quiet) {
            int ended = 1;

            for (j = 0; j < count; j++) {
                if (!(files[j].eof || (!has_data(&files[j]) && !page.full)))
                    ended = 0;
            }
            if (ended) {
                fprintf(stderr, "%s: starting page number %lld exceeds page count %llu\n", PROGRAM, p->firstPage, p->pageNumber);
                p->quiet = 0;
                break;
            }
        }
        for (j = 0; j < count; j++) {
            if (files[j].status == FF_FOUND)
                ff = 1;
        }
        prevFull = !ff;
        for (j = 0; j < count; j++) {
            if (files[j].status != OPEN)
                prevFull = 0;
        }
        if (p->quiet)
            skipped++;
        print_page(p, &page, ff, dateText, fileText);
        p->quiet = 0;
    }
    free_page(&page);
}

/* ---- the options ---- */

/** Reads a number the way pr's options do. Returns 0, or -1 after printing the message. */
static int parse_number(const char* text, long long min, long long max, const char* what, long long* out) {
    const char* s = text;
    int negative = 0;
    unsigned long long value = 0;
    int overflow = 0;

    if (*s == '-' || *s == '+') {
        negative = *s == '-';
        s++;
    }
    if (!isdigit((unsigned char)*s)) {
        fprintf(stderr, "%s: %s: '%s'\n", PROGRAM, what, text);
        return -1;
    }
    for (; isdigit((unsigned char)*s); s++) {
        if (value > (ULLONG_MAX - 9) / 10)
            overflow = 1;
        else
            value = value * 10 + (unsigned)(*s - '0');
    }
    if (*s != '\0') {
        fprintf(stderr, "%s: %s: '%s'\n", PROGRAM, what, text);
        return -1;
    }
    if (overflow || value > (unsigned long long)LLONG_MAX) {
        fprintf(stderr, "%s: %s: '%s': Value too large for defined data type\n", PROGRAM, what, text);
        return -1;
    }
    *out = negative ? -(long long)value : (long long)value;
    if (*out < min) {
        fprintf(stderr, "%s: %s: '%s': Numerical result out of range\n", PROGRAM, what, text);
        return -1;
    }
    if (*out > max) {
        fprintf(stderr, "%s: %s: '%s': Value too large for defined data type\n", PROGRAM, what, text);
        return -1;
    }
    return 0;
}

/** The argument of -e, -i and -n: an optional character (not a digit) and an optional width. */
static int tab_argument(const char* argument, char letter, char* character, int* number) {
    const char* s = argument;

    if (!isdigit((unsigned char)*s))
        *character = *s++;
    if (*s != '\0') {
        const char* t = s;
        long long value = 0;
        int ok = isdigit((unsigned char)*t) != 0;

        for (; isdigit((unsigned char)*t); t++) {
            value = value * 10 + (*t - '0');
            if (value > INT_MAX)
                ok = 0;
        }
        if (*t != '\0' || value <= 0)
            ok = 0;
        if (!ok) {
            fprintf(stderr, "%s: '-%c' extra characters or invalid number in the argument: '%s'\n", PROGRAM, letter, s);
            args_usage_error(&spec);
            return -1;
        }
        *number = (int)value;
    }
    return 0;
}

static int page_operand(Pr* p, const char* arg);

static int on_option(int id, const char* value, void* context) {
    Pr* p = (Pr*)context;
    long long n;
    int wasNumber;

    wasNumber = p->lastWasNumber;
    p->lastWasNumber = id == ARGS_NUMBER;
    switch (id) {
    case ARGS_NUMBER:
    case OPT_COLUMNS:
        if (id == ARGS_NUMBER && wasNumber && strlen(p->digits) + strlen(value) < sizeof p->digits)
            strcat(p->digits, value);
        else
            snprintf(p->digits, sizeof p->digits, "%s", value);
        if (parse_number(id == ARGS_NUMBER ? p->digits : value, 1, INT_MAX, "invalid number of columns", &n) != 0)
            return -1;
        p->columns = (int)n;
        p->columnsSet = 1;
        break;
    case OPT_PAGES: {
        char text[64];
        int kind;

        snprintf(text, sizeof text, "+%s", value);
        kind = page_operand(p, text);
        if (kind <= 0) {
            if (kind == 0)
                fprintf(stderr, "%s: invalid page range '%s'\n", PROGRAM, value);
            return -1;
        }
        break;
    }
    case OPT_ACROSS: p->across = 1; break;
    case OPT_BALANCE: break;
    case OPT_CONTROL: p->showControl = 1; break;
    case OPT_DOUBLE: p->doubleSpace = 1; break;
    case OPT_DATE: p->dateFormat = value; break;
    case OPT_EXPAND:
        p->untabify = 1;
        if (value != NULL && tab_argument(value, 'e', &p->inTabChar, &p->inTabWidth) != 0)
            return -1;
        break;
    case OPT_FORM: p->useFormFeed = 1; break;
    case OPT_HEADER: p->header = value; break;
    case OPT_OUTTABS:
        p->tabifyOption = 1;
        if (value != NULL && tab_argument(value, 'i', &p->outTabChar, &p->outTabWidth) != 0)
            return -1;
        break;
    case OPT_JOIN: p->joinLines = 1; break;
    case OPT_LENGTH:
        if (parse_number(value, 1, INT_MAX, "'-l PAGE_LENGTH' invalid number of lines", &n) != 0)
            return -1;
        p->pageLength = (int)n;
        break;
    case OPT_MERGE: p->parallel = 1; break;
    case OPT_NUMBER:
        p->numbered = 1;
        if (value != NULL && tab_argument(value, 'n', &p->numberSep, &p->numberChars) != 0)
            return -1;
        break;
    case OPT_FIRST:
        if (parse_number(value, INT_MIN, INT_MAX, "'-N NUMBER' invalid starting line number", &n) != 0)
            return -1;
        p->startLine = (int)n;
        p->startLineSet = 1;
        break;
    case OPT_INDENT:
        if (parse_number(value, 0, INT_MAX, "'-o MARGIN' invalid line offset", &n) != 0)
            return -1;
        p->margin = (int)n;
        break;
    case OPT_NOWARN: p->noWarnings = 1; break;
    case OPT_SEPARATOR:
        if (value != NULL || !p->sGiven) {
            p->sText = value != NULL ? value : "\t";
            p->sHasArg = value != NULL;
        }
        p->sGiven = 1;
        break;
    case OPT_SEPSTRING:
        p->SGiven = 1;
        p->SText = value != NULL ? value : "";
        break;
    case OPT_OMIT: p->omitHeader = 1; break;
    case OPT_NOPAGE: p->omitHeader = 1; p->omitPagination = 1; break;
    case OPT_NONPRINT: p->showNonprinting = 1; break;
    case OPT_WIDTH:
        if (parse_number(value, 1, INT_MAX, "'-w PAGE_WIDTH' invalid number of characters", &n) != 0)
            return -1;
        p->wGiven = 1;
        p->widthW = (int)n;
        p->forceTruncate = 0;
        break;
    case OPT_PAGEWIDTH:
        if (parse_number(value, 1, INT_MAX, "'-W PAGE_WIDTH' invalid number of characters", &n) != 0)
            return -1;
        p->pageWidthGiven = 1;
        p->widthBig = (int)n;
        p->forceTruncate = 1;
        break;
    }
    return 0;
}

/** A + operand: +FIRST[:LAST]. Returns 1 when it was one, 0 when it names a file, -1 for an error. */
static int page_operand(Pr* p, const char* arg) {
    const char* s = arg + 1;
    const char* t;
    unsigned long long first = 0, last = 0;
    int haveLast = 0;

    if (!isdigit((unsigned char)*s)) {
        fprintf(stderr, "%s: invalid + argument '%s'\n", PROGRAM, s);
        return -1;
    }
    for (t = s; isdigit((unsigned char)*t); t++) {
        if (first > (ULLONG_MAX - 9) / 10)
            first = ULLONG_MAX / 2;
        else
            first = first * 10 + (unsigned)(*t - '0');
    }
    if (*t == ':') {
        t++;
        if (!isdigit((unsigned char)*t)) {
            fprintf(stderr, "%s: invalid + argument '%s'\n", PROGRAM, s);
            return -1;
        }
        for (; isdigit((unsigned char)*t); t++) {
            if (last > (ULLONG_MAX - 9) / 10)
                last = ULLONG_MAX / 2;
            else
                last = last * 10 + (unsigned)(*t - '0');
        }
        haveLast = 1;
    }
    if (*t != '\0') {
        fprintf(stderr, "%s: invalid + argument '%s'\n", PROGRAM, s);
        return -1;
    }
    if (first == 0 || (haveLast && last < first))
        return 0;
    p->firstPage = (long long)first;
    p->lastPage = haveLast ? (long long)last : 0;
    return 1;
}

/** The values that follow from the options (pr's init_parameters). Returns 0, or -1 after a message. */
static int derive(Pr* p, int fileCount) {
    int usable;

    p->extremities = !p->omitHeader;
    p->keepFF = p->omitHeader && !p->omitPagination;
    p->linesPerBody = p->pageLength - HEADER_LINES - FOOTER_LINES;
    if (p->linesPerBody <= 0) {
        p->extremities = 0;
        p->keepFF = 1;
    }
    if (!p->extremities)
        p->linesPerBody = p->pageLength;
    if (p->doubleSpace)
        p->linesPerBody /= 2;
    if (p->parallel)
        p->columns = fileCount;
    if (p->columns < 1)
        p->columns = 1;
    p->multi = p->columns > 1;
    p->accurateTabs = p->multi && !p->across && !p->parallel;
    p->charsPerLine = p->pageWidthGiven ? p->widthBig : (p->wGiven ? p->widthW : CHARS_PER_LINE);
    p->tabify = p->tabifyOption || p->multi;

    /* the separator */
    p->aligned = 1;
    p->colSep = " ";
    if (p->SGiven) {
        p->colSep = p->SText;
    } else if (p->sGiven) {
        p->colSep = (p->wGiven || p->pageWidthGiven) && !p->sHasArg ? "" : p->sText;
        p->aligned = p->wGiven || p->pageWidthGiven;
    } else if (p->joinLines) {
        p->colSep = "\t";
    }
    if (p->joinLines)
        p->aligned = 0;
    if (p->joinLines && !p->SGiven && !p->sGiven)
        p->colSep = "\t";
    p->colSepLen = (int)strlen(p->colSep);

    /* the width of the columns */
    p->truncateLines = p->multi || p->parallel;
    if (p->sGiven && !p->SGiven && !p->wGiven && !p->pageWidthGiven)
        p->truncateLines = 0;
    if (p->forceTruncate)
        p->truncateLines = 1;
    if (p->joinLines)
        p->truncateLines = 0;

    if (p->numbered) {
        if (p->numberSep == '\t')
            p->numberWidth = p->numberChars + TAB_WIDTH(p->inTabWidth, p->numberChars);
        else
            p->numberWidth = p->numberChars + 1;
    }
    usable = p->charsPerLine - (p->multi ? (p->columns - 1) * p->colSepLen : 0);
    if (p->parallel && p->numbered)
        usable -= p->numberWidth;
    p->charsPerColumn = p->multi || p->parallel ? usable / p->columns : p->charsPerLine;
    if (p->parallel && !p->multi && p->numbered)
        p->charsPerColumn = usable;
    if (p->charsPerColumn < 1) {
        fprintf(stderr, "%s: page width too narrow\n", PROGRAM);
        return -1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    Pr pr;
    int result, i, status = 0;
    const char** operands;
    int count;
    const char** names;
    int nameCount = 0;
    char dateText[128];
    Input* files;
    int opened = 0;
    int pagesSeen = 0;

    memset(&pr, 0, sizeof pr);
    pr.columns = 1;
    pr.numberChars = 5;
    pr.numberSep = '\t';
    pr.inTabChar = '\t';
    pr.outTabChar = '\t';
    pr.inTabWidth = 8;
    pr.outTabWidth = 8;
    pr.pageLength = 66;
    pr.dateFormat = "%Y-%m-%d %H:%M";
    pr.firstPage = 1;

    text_binary_streams();
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);

    result = args_parse(&spec, argc, argv, on_option, &pr, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    names = (const char**)calloc((size_t)count + 2, sizeof(char*));
    for (i = 0; i < count; i++) {
        if (operands[i][0] == '+' && !pagesSeen) {
            int kind = page_operand(&pr, operands[i]);

            if (kind < 0) {
                args_usage_error(&spec);
                return 1;
            }
            if (kind == 1) {
                pagesSeen = 1;
                continue;
            }
        }
        names[nameCount++] = operands[i];
    }
    if (nameCount == 0)
        names[nameCount++] = "-";
    if (pr.parallel && pr.columnsSet && pr.columns > 1) {
        fprintf(stderr, "%s: cannot specify number of columns when printing in parallel\n", PROGRAM);
        return 1;
    }
    if (pr.across && pr.parallel) {
        fprintf(stderr, "%s: cannot specify both printing across and printing in parallel\n", PROGRAM);
        return 1;
    }

    files = (Input*)calloc((size_t)nameCount + 1, sizeof(Input));
    if (pr.parallel) {
        for (i = 0; i < nameCount; i++) {
            if (open_input(&pr, names[i], &files[opened]) == 0)
                opened++;
            else
                status = 1;
        }
        if (opened == 0)
            return 1;
        if (derive(&pr, opened) != 0)
            return 1;
        format_date(&pr, datetime_now(), dateText, sizeof dateText);
        run_pages(&pr, files, opened, dateText, pr.header != NULL ? pr.header : "");
    } else {
        if (derive(&pr, 1) != 0)
            return 1;
        for (i = 0; i < nameCount; i++) {
            Input one;
            const char* shown;

            if (open_input(&pr, names[i], &one) != 0) {
                status = 1;
                continue;
            }
            format_date(&pr, one.modified, dateText, sizeof dateText);
            shown = pr.header != NULL ? pr.header : (one.name != NULL ? one.name : "");
            run_pages(&pr, &one, 1, dateText, shown);
            free(one.data);
        }
    }
    fflush(stdout);
    return status;
}
