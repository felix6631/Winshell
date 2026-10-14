/*
 * Running the script of sed over the input.
 *
 * A cycle reads a line into the pattern space, runs the commands, prints the pattern space (unless -n)
 * and writes what a, r and R queued. A line is what text_reader gives: its ending (LF, CR LF or none
 * on the last line) is kept and written back, so a file with CR LF lines stays one.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "fsutil.h"
#include "sed.h"
#include "textutil.h"
#include "utf8.h"
#include "vpath.h"

#define PROGRAM "sed"

typedef struct {
    Buf    text;
    int    ending;
    size_t file;
    int    valid;
} Line;

struct Reader {
    FILE*      file;
    LineReader lines;
    int        open;
};

typedef struct {
    int            kind;                // 't' text, 'r' file
    unsigned char* data;                // the text, or the file name
    size_t         length;
} Append;

typedef enum { CYCLE_END, CYCLE_DELETE, CYCLE_RESTART, CYCLE_QUIT, CYCLE_QUIT_SILENT } CycleResult;

/** Everything one run of the script keeps: the input, the pattern and hold space, the output. */
typedef struct {
    Script*          script;
    const SedConfig* config;
    int              terminator;

    const char* const* inputFiles;      // of the stream
    size_t           inputCount, nextInput;
    FILE*            currentFile;
    LineReader       reader;
    int              readerOpen;
    size_t           readerIndex;
    const char* const* allNames;        // for F: the names of all the files
    Line             look;              // the line read ahead

    Buf              ps, hs;            // the pattern space and the hold space
    int              psEnding, hsEnding;
    size_t           psFile;
    size_t           streamBase;        // the index in the list of all files of the first one of the stream
    long             lineNumber;
    int              replaced;          // for t and T
    Regex*           lastRegex;
    RegexInput       regexInput;

    Output           stdoutOutput;      // standard output
    Output*          out;               // where the lines go: standard output, or the file that -i edits
    Append*          appends;
    size_t           appendCount, appendCapacity;

    int              exitStatus;
    int              quitStatus;
    int              quitting;
} Executor;

/* ---- output -------------------------------------------------------------------------------- */

Output* sed_output_open(Script* script, const char* name) {
    Output* o;
    char native[VPATH_MAX];

    for (o = script->outputs; o != NULL; o = o->next)
        if (strcmp(o->name, name) == 0)
            return o;

    o = calloc(1, sizeof *o);
    if (o == NULL)
        return NULL;
    // like GNU sed, w /dev/stdout is an output of its own: the end of a last line that has none is
    // not made up for by what the program writes to standard output otherwise
    if (strcmp(name, "/dev/stdout") == 0) {
        o->file = stdout;
    } else if (strcmp(name, "/dev/stderr") == 0) {
        o->file = stderr;
    } else {
        if (vpath_to_native(name, native, sizeof native) != 0) {
            free(o);
            return NULL;
        }
        o->file = fopen(native, "wb");
        if (o->file == NULL) {
            free(o);
            return NULL;
        }
    }
    o->name = _strdup(name);
    o->next = script->outputs;
    script->outputs = o;
    return o;
}

static void put_missing_newline(Executor* x, Output* o) {
    if (o->missingNewline) {
        fputc(x->terminator, o->file);
        o->missingNewline = 0;
    }
}

static void finish_write(Executor* x, Output* o) {
    if (x->config->unbuffered || o->file == stderr)
        fflush(o->file);
}

static void write_raw(Executor* x, Output* o, const void* data, size_t length) {
    put_missing_newline(x, o);
    fwrite(data, 1, length, o->file);
    finish_write(x, o);
}

/** A line with its ending: 0 none (nothing follows, until more is written), 1 the x->terminator, 2 CR LF. */
static void write_line(Executor* x, Output* o, const void* data, size_t length, int ending) {
    put_missing_newline(x, o);
    fwrite(data, 1, length, o->file);
    if (ending == 2)
        fputs("\r\n", o->file);
    else if (ending == 1)
        fputc(x->terminator, o->file);
    else
        o->missingNewline = 1;
    finish_write(x, o);
}

static void print_pattern(Executor* x, Output* o) {
    write_line(x, o, x->ps.data, x->ps.length, x->psEnding);
}

static void queue_append(Executor* x, int kind, const void* data, size_t length) {
    Append* a;
    if (VEC_RESERVE(x->appends, x->appendCapacity, x->appendCount + 1) != 0)
        return;
    a = &x->appends[x->appendCount++];
    a->kind = kind;
    a->data = malloc(length + 1);
    if (a->data == NULL) {
        x->appendCount--;
        return;
    }
    memcpy(a->data, data, length);
    a->data[length] = '\0';
    a->length = length;
}

static void dump_append(Executor* x) {
    size_t i;
    for (i = 0; i < x->appendCount; i++) {
        Append* a = &x->appends[i];
        if (a->kind == 't') {
            write_raw(x, x->out, a->data, a->length);
        } else {
            char native[VPATH_MAX];
            if (vpath_to_native((const char*)a->data, native, sizeof native) == 0) {
                FILE* f = fopen(native, "rb");
                if (f != NULL) {
                    unsigned char chunk[8192];
                    size_t n;
                    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
                        write_raw(x, x->out, chunk, n);
                    fclose(f);
                }
            }
        }
        free(a->data);
    }
    x->appendCount = 0;
}

/* ---- input --------------------------------------------------------------------------------- */

/** Opens an input file; a failure is reported and sets the exit status. */
static int open_input(Executor* x, const char* name, FILE** file) {
    char native[VPATH_MAX];
    const char* reason;
    DWORD attributes;

    if (strcmp(name, "-") == 0) {
        *file = stdin;
        return 0;
    }
    if (vpath_to_native(name, native, sizeof native) == VPATH_ROOT) {
        fprintf(stderr, PROGRAM ": couldn't edit %s: not a regular file\n", name);
        x->exitStatus = x->config->inPlace ? 4 : 2;
        return -1;
    }
    if (vpath_to_native(name, native, sizeof native) != 0) {
        reason = strerror(errno);
    } else if (fs_attributes(native, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        if (x->config->inPlace)
            fprintf(stderr, PROGRAM ": couldn't edit %s: not a regular file\n", name);
        else
            fprintf(stderr, PROGRAM ": read error on %s: Is a directory\n", name);
        x->exitStatus = x->config->inPlace ? 4 : 2;
        return -1;
    } else {
        *file = fopen(native, "rb");
        if (*file != NULL)
            return 0;
        reason = fs_strerror(GetLastError());
        if (errno == ENOENT)
            reason = "No such file or directory";
    }
    fprintf(stderr, PROGRAM ": can't read %s: %s\n", name, reason);
    x->exitStatus = 2;
    return -1;
}

/** The next line of the input files of the stream, into line. Returns 0 at the end. */
static int fetch(Executor* x, Line* line) {
    for (;;) {
        TextLine text;

        if (!x->readerOpen) {
            if (x->nextInput >= x->inputCount)
                return 0;
            x->readerIndex = x->nextInput;
            if (open_input(x, x->inputFiles[x->nextInput++], &x->currentFile) != 0)
                continue;
            text_reader_init(&x->reader, x->currentFile, x->terminator);
            x->readerOpen = 1;
        }
        if (text_reader_next(&x->reader, &text) > 0) {
            line->text.length = 0;
            buf_add(&line->text, text.content, text.length);
            line->ending = text.ending;
            line->file = x->readerIndex;
            line->valid = 1;
            return 1;
        }
        text_reader_free(&x->reader);
        if (x->currentFile != stdin)
            fclose(x->currentFile);
        x->readerOpen = 0;
    }
}

static int has_next(Executor* x) {
    if (x->look.valid)
        return 1;
    return fetch(x, &x->look);
}

/** Reads the next line into the pattern space (after it, with a x->terminator between, when append). */
static int read_pattern_space(Executor* x, int append) {
    dump_append(x);
    if (!x->look.valid && !fetch(x, &x->look))
        return 0;
    x->look.valid = 0;

    if (append)
        buf_add_byte(&x->ps, x->terminator);
    else
        x->ps.length = 0;
    buf_add(&x->ps, x->look.text.data, x->look.text.length);
    x->psEnding = x->look.ending;
    x->psFile = x->look.file;
    x->lineNumber++;
    x->replaced = 0;
    return 1;
}

/* ---- addresses ----------------------------------------------------------------------------- */

static int load_input(Executor* x) {
    return regex_input_set(&x->regexInput, x->ps.data, x->ps.length);
}

static int regex_matches(Executor* x, Regex* re) {
    size_t start, end;

    if (re == NULL)
        re = x->lastRegex;
    if (re == NULL) {
        fprintf(stderr, PROGRAM ": no previous regular expression\n");
        exit(1);
    }
    x->lastRegex = re;
    if (load_input(x) != 0)
        return 0;
    return regex_search(re, &x->regexInput, 0, &start, &end);
}

static int is_last_line(Executor* x) {
    return !has_next(x);
}

static int address_matches(Executor* x, const Address* a) {
    switch (a->kind) {
        case ADDR_LINE:
            return x->lineNumber == a->number;
        case ADDR_LAST:
            return is_last_line(x);
        case ADDR_REGEX:
            return regex_matches(x, a->regex);
        case ADDR_STEP:
            if (a->step <= 0)
                return x->lineNumber == a->number;
            return x->lineNumber >= a->number && (x->lineNumber - a->number) % a->step == 0;
        default:
            return 0;
    }
}

static int command_matches(Executor* x, Command* c) {
    int matched;

    if (c->first.kind == ADDR_NONE)
        return !c->negate;

    if (c->second.kind == ADDR_NONE) {
        matched = address_matches(x, &c->first);
    } else if (c->rangeActive) {
        matched = 1;
        switch (c->second.kind) {
            case ADDR_LINE:
            case ADDR_PLUS:
            case ADDR_MULTIPLE:
                if (x->lineNumber >= c->rangeEnd) {
                    c->rangeActive = 0;
                    matched = x->lineNumber == c->rangeEnd;
                }
                break;
            default:
                if (address_matches(x, &c->second))
                    c->rangeActive = 0;
                break;
        }
    } else {
        matched = address_matches(x, &c->first);
        if (c->first.kind == ADDR_ZERO)
            matched = 0;
        if (matched) {
            switch (c->second.kind) {
                case ADDR_LINE:
                    if (c->second.number > x->lineNumber) {
                        c->rangeActive = 1;
                        c->rangeEnd = c->second.number;
                    }
                    break;
                case ADDR_PLUS:
                    if (c->second.number > 0) {
                        c->rangeActive = 1;
                        c->rangeEnd = x->lineNumber + c->second.number;
                    }
                    break;
                case ADDR_MULTIPLE:
                    if (c->second.number > 0 && x->lineNumber % c->second.number != 0) {
                        c->rangeActive = 1;
                        c->rangeEnd = (x->lineNumber / c->second.number + 1) * c->second.number;
                    }
                    break;
                case ADDR_LAST:
                    c->rangeActive = !is_last_line(x);
                    break;
                default:
                    c->rangeActive = 1;     // a regular expression is looked for from the next line on
                    break;
            }
        }
    }
    return matched != c->negate;
}

static void reset_ranges(Executor* x) {
    size_t i;
    for (i = 0; i < x->script->count; i++) {
        Command* c = &x->script->commands[i];
        c->rangeActive = c->first.kind == ADDR_ZERO;
    }
}

/* ---- commands ------------------------------------------------------------------------------ */

typedef struct {
    int mode;           // 0, 'U' or 'L'
    int once;           // 0, 'u' or 'l': for the next character only
} CaseState;

static void add_with_case(Buf* dst, const unsigned char* s, size_t length, CaseState* state) {
    size_t i = 0;

    if (state->mode == 0 && state->once == 0) {
        buf_add(dst, s, length);
        return;
    }
    while (i < length) {
        size_t n;
        uint32_t c = utf8_decode(s + i, length - i, &n);
        int kind = state->once != 0 ? state->once : state->mode;
        char encoded[4];

        if (c < UTF8_INVALID_BASE) {
            if (kind == 'U' || kind == 'u')
                c = utf8_upper(c);
            else if (kind == 'L' || kind == 'l')
                c = utf8_fold(c);
            buf_add(dst, encoded, utf8_encode(c, encoded));
        } else {
            buf_add(dst, s + i, n);
        }
        state->once = 0;
        i += n;
    }
}

/** The right hand side of s for one match: its text, groups (spans, or the match when 0) and case changes. */
static void append_replacement(Executor* x, Buf* result, const Substitution* sub, size_t start, size_t end, const size_t* spans) {
    CaseState state = { 0, 0 };
    size_t k;

    for (k = 0; k < sub->partCount; k++) {
        const Part* part = &sub->parts[k];

        if (part->kind == PART_TEXT) {
            add_with_case(result, part->text, part->length, &state);
        } else if (part->kind == PART_GROUP) {
            size_t from = start, to = end;
            if (part->number > 0) {
                from = spans != NULL ? spans[2 * part->number] : (size_t)-1;
                to = spans != NULL ? spans[2 * part->number + 1] : (size_t)-1;
            }
            if (from != (size_t)-1 && to != (size_t)-1 && from <= to)
                add_with_case(result, x->ps.data + x->regexInput.offset[from],
                              x->regexInput.offset[to] - x->regexInput.offset[from], &state);
        } else if (part->number == 'E') {
            state.mode = 0;
            state.once = 0;
        } else if (part->number == 'U' || part->number == 'L') {
            state.mode = part->number;
        } else {
            state.once = part->number;
        }
    }
}

/** s: returns 1 when something was x->replaced. */
static int substitute(Executor* x, const Substitution* sub) {
    Regex* re = sub->regex != NULL ? sub->regex : x->lastRegex;
    Buf result;
    size_t* spans = NULL;
    size_t position = 0, copied = 0, previousEnd = (size_t)-1, start, end;
    long count = 0;
    int didReplace = 0;

    if (re == NULL) {
        fprintf(stderr, PROGRAM ": no previous regular expression\n");
        exit(1);
    }
    x->lastRegex = re;
    if (load_input(x) != 0)
        return 0;
    if (sub->needGroups) {
        spans = malloc((regex_group_count(re) + 1) * 2 * sizeof *spans);
        if (spans == NULL)
            return 0;
    }
    memset(&result, 0, sizeof result);

    while (position <= x->regexInput.length && regex_search(re, &x->regexInput, position, &start, &end)) {
        if (start == end && start == previousEnd) {
            // an empty match right after the previous one does not count
            if (start >= x->regexInput.length)
                break;
            position = start + 1;
            continue;
        }
        count++;
        if (count >= sub->number) {
            buf_add(&result, x->ps.data + x->regexInput.offset[copied], x->regexInput.offset[start] - x->regexInput.offset[copied]);
            if (spans != NULL)
                regex_groups(re, &x->regexInput, start, end, spans);
            append_replacement(x, &result, sub, start, end, spans);
            copied = end;
            didReplace = 1;
            if (!sub->global)
                break;
        }
        previousEnd = end;
        if (start == end) {
            if (end >= x->regexInput.length)
                break;
            position = end + 1;
        } else {
            position = end;
        }
    }

    if (didReplace) {
        buf_add(&result, x->ps.data + x->regexInput.offset[copied], x->ps.length - x->regexInput.offset[copied]);
        free(x->ps.data);
        x->ps = result;
    } else {
        free(result.data);
    }
    free(spans);
    return didReplace;
}

static void translate(Executor* x, const Command* c) {
    Buf result;
    size_t i = 0, k;

    memset(&result, 0, sizeof result);
    while (i < x->ps.length) {
        size_t n;
        uint32_t ch = utf8_decode(x->ps.data + i, x->ps.length - i, &n);
        int mapped = 0;

        for (k = 0; k < c->yCount; k++) {
            if (c->yFrom[k] == ch) {
                char encoded[4];
                buf_add(&result, encoded, utf8_encode(c->yTo[k], encoded));
                mapped = 1;
                break;
            }
        }
        if (!mapped)
            buf_add(&result, x->ps.data + i, n);
        i += n;
    }
    free(x->ps.data);
    x->ps = result;
}

/** l: the pattern space with the characters that do not show written as escapes, wrapped. */
static void list_pattern(Executor* x, long width) {
    size_t i;
    long column = 0;
    char piece[8];

    if (width <= 1)
        width = 0;
    for (i = 0; i < x->ps.length; i++) {
        unsigned char c = x->ps.data[i];
        size_t length;

        switch (c) {
            case '\\': strcpy(piece, "\\\\"); break;
            case '\a': strcpy(piece, "\\a"); break;
            case '\b': strcpy(piece, "\\b"); break;
            case '\f': strcpy(piece, "\\f"); break;
            case '\n': strcpy(piece, "\\n"); break;
            case '\r': strcpy(piece, "\\r"); break;
            case '\t': strcpy(piece, "\\t"); break;
            case '\v': strcpy(piece, "\\v"); break;
            default:
                if (c >= 32 && c < 127)
                    sprintf(piece, "%c", c);
                else
                    sprintf(piece, "\\%03o", c);
                break;
        }
        length = strlen(piece);
        if (width > 0 && column + (long)length > width - 1) {
            write_raw(x, x->out, "\\\n", 2);
            column = 0;
        }
        write_raw(x, x->out, piece, length);
        column += (long)length;
    }
    write_raw(x, x->out, "$\n", 2);
}

static struct Reader* reader_for(Executor* x, Command* c) {
    struct Reader* r = c->reader;

    if (r == NULL) {
        char native[VPATH_MAX];
        r = calloc(1, sizeof *r);
        if (r == NULL)
            return NULL;
        c->reader = r;
        if (vpath_to_native((const char*)c->text, native, sizeof native) == 0) {
            r->file = fopen(native, "rb");
            if (r->file != NULL) {
                text_reader_init(&r->lines, r->file, x->terminator);
                r->open = 1;
            }
        }
    }
    return r;
}

static size_t find_terminator(Executor* x) {
    size_t i;
    for (i = 0; i < x->ps.length; i++)
        if (x->ps.data[i] == x->terminator)
            return i;
    return (size_t)-1;
}

/** g G h H x: the hold space. The ending goes with the text: a last line without one gets the one of what it is mixed with. */
static void hold_command(Executor* x, int name) {
    switch (name) {
        case 'g':
            x->ps.length = 0;
            buf_add(&x->ps, x->hs.data, x->hs.length);
            x->psEnding = x->hsEnding;
            break;
        case 'G':
            buf_add_byte(&x->ps, x->terminator);
            buf_add(&x->ps, x->hs.data, x->hs.length);
            if (x->hsEnding == 0)
                x->psEnding = 0;
            else if (x->psEnding == 0)
                x->psEnding = x->hsEnding;
            break;
        case 'h':
            x->hs.length = 0;
            buf_add(&x->hs, x->ps.data, x->ps.length);
            x->hsEnding = x->psEnding;
            break;
        case 'H':
            buf_add_byte(&x->hs, x->terminator);
            buf_add(&x->hs, x->ps.data, x->ps.length);
            if (x->psEnding == 0)
                x->hsEnding = 0;
            else if (x->hsEnding == 0)
                x->hsEnding = x->psEnding;
            break;
        default: {      // x
            Buf t = x->ps;
            int e = x->psEnding;
            x->ps = x->hs;
            x->hs = t;
            x->psEnding = x->hsEnding;
            x->hsEnding = e;
            break;
        }
    }
}

/** P and W: the pattern space up to its first line feed. */
static void print_first_line(Executor* x, Output* o) {
    size_t at = find_terminator(x);
    if (at == (size_t)-1)
        write_line(x, o, x->ps.data, x->ps.length, x->psEnding);
    else
        write_line(x, o, x->ps.data, at, x->psEnding == 2 ? 2 : 1);
}

/** D: deletes up to the first line feed and starts over with what is left, or acts as d. */
static CycleResult delete_first_line(Executor* x) {
    size_t at = find_terminator(x);
    if (at == (size_t)-1) {
        x->ps.length = 0;
        return CYCLE_DELETE;
    }
    memmove(x->ps.data, x->ps.data + at + 1, x->ps.length - at - 1);
    x->ps.length -= at + 1;
    return CYCLE_RESTART;
}

/** R: queues the next line of the file. */
static void queue_next_line(Executor* x, Command* c) {
    struct Reader* r = reader_for(x, c);
    TextLine line;
    Buf text;

    if (r == NULL || !r->open || text_reader_next(&r->lines, &line) <= 0)
        return;
    memset(&text, 0, sizeof text);
    buf_add(&text, line.content, line.length);
    buf_add_byte(&text, '\n');
    queue_append(x, 't', text.data, text.length);
    free(text.data);
}

/** s: substitutes and does what the flags ask for. */
static void substitute_command(Executor* x, const Command* c) {
    if (!substitute(x, c->sub))
        return;
    x->replaced = 1;
    if (c->sub->print)
        print_pattern(x, x->out);
    if (c->sub->out != NULL)
        print_pattern(x, c->sub->out);
}

/** The commands that only do something to the pattern space, the hold space or the output. */
static void run_plain_command(Executor* x, Command* c) {
    switch (c->name) {
        case '=': {
            char number[32];
            int n = sprintf(number, "%ld\n", x->lineNumber);
            write_raw(x, x->out, number, (size_t)n);
            break;
        }
        case 'a':
            queue_append(x, 't', c->text, c->length);
            break;
        case 'i':
            write_raw(x, x->out, c->text, c->length);
            break;
        case 'F': {
            const char* name = x->allNames[x->streamBase + x->psFile];
            write_raw(x, x->out, name, strlen(name));
            write_raw(x, x->out, "\n", 1);
            break;
        }
        case 'g': case 'G': case 'h': case 'H': case 'x':
            hold_command(x, c->name);
            break;
        case 'l':
            list_pattern(x, c->number >= 0 ? c->number : x->config->lineWrap);
            break;
        case 'p':
            print_pattern(x, x->out);
            break;
        case 'P':
            print_first_line(x, x->out);
            break;
        case 'r':
            queue_append(x, 'r', c->text, c->length);
            break;
        case 'R':
            queue_next_line(x, c);
            break;
        case 's':
            substitute_command(x, c);
            break;
        case 'w':
            print_pattern(x, c->out);
            break;
        case 'W':
            print_first_line(x, c->out);
            break;
        case 'y':
            translate(x, c);
            break;
        case 'z':
            x->ps.length = 0;
            break;
        default:        // { } : v and the commands that change the flow
            break;
    }
}

typedef enum { FLOW_NEXT, FLOW_JUMP, FLOW_LEAVE } Flow;

/**
 * The commands that decide what happens next: end the cycle (c d D n N q Q) or jump (b t T).
 * FLOW_NEXT goes on with the next command, FLOW_JUMP sets *pc, FLOW_LEAVE ends the program with *result.
 */
static Flow run_flow_command(Executor* x, Command* c, size_t* pc, CycleResult* result) {
    switch (c->name) {
        case 'c':
            if (c->second.kind == ADDR_NONE || c->negate || !c->rangeActive)
                write_raw(x, x->out, c->text, c->length);
            x->ps.length = 0;
            *result = CYCLE_DELETE;
            return FLOW_LEAVE;
        case 'd':
            x->ps.length = 0;
            *result = CYCLE_DELETE;
            return FLOW_LEAVE;
        case 'D':
            *result = delete_first_line(x);
            return FLOW_LEAVE;
        case 'n':
            if (!has_next(x)) {
                *result = CYCLE_END;
                return FLOW_LEAVE;
            }
            if (!x->config->quiet)
                print_pattern(x, x->out);
            read_pattern_space(x, 0);
            return FLOW_NEXT;
        case 'N':
            if (!has_next(x)) {
                *result = CYCLE_END;
                return FLOW_LEAVE;
            }
            read_pattern_space(x, 1);
            return FLOW_NEXT;
        case 'q':
            x->quitStatus = c->number >= 0 ? (int)c->number : 0;
            *result = CYCLE_QUIT;
            return FLOW_LEAVE;
        case 'Q':
            x->quitStatus = c->number >= 0 ? (int)c->number : 0;
            *result = CYCLE_QUIT_SILENT;
            return FLOW_LEAVE;
        case 't':
            if (!x->replaced)
                return FLOW_NEXT;
            x->replaced = 0;
            *pc = c->target;
            return FLOW_JUMP;
        case 'T':
            if (!x->replaced) {
                *pc = c->target;
                return FLOW_JUMP;
            }
            x->replaced = 0;
            return FLOW_NEXT;
        case 'b':
            *pc = c->target;
            return FLOW_JUMP;
        default:
            return FLOW_NEXT;
    }
}

static int is_flow_command(int name) {
    return strchr("cdDnNqQtTb", name) != NULL;
}

static CycleResult execute_program(Executor* x) {
    size_t pc = 0;

    while (pc < x->script->count) {
        Command* c = &x->script->commands[pc];

        if (!command_matches(x, c)) {
            pc = c->name == '{' ? c->target + 1 : pc + 1;
            continue;
        }

        if (is_flow_command(c->name)) {
            CycleResult result = CYCLE_END;
            Flow flow = run_flow_command(x, c, &pc, &result);

            if (flow == FLOW_LEAVE)
                return result;
            if (flow == FLOW_JUMP)
                continue;
        } else {
            run_plain_command(x, c);
        }
        pc++;
    }
    return CYCLE_END;
}

/* ---- the streams --------------------------------------------------------------------------- */

static void run_stream(Executor* x, const char* const* files, size_t count, size_t firstIndex) {
    int needRead = 1;

    x->inputFiles = files;
    x->inputCount = count;
    x->nextInput = 0;
    x->readerOpen = 0;
    x->look.valid = 0;
    x->streamBase = firstIndex;

    for (;;) {
        CycleResult result;

        if (needRead && !read_pattern_space(x, 0))
            break;
        needRead = 1;

        result = execute_program(x);
        if (result == CYCLE_QUIT && x->psEnding == 0)
            x->psEnding = 1;               // like GNU sed, q ends the last line even when it had no ending
        if (result == CYCLE_END || result == CYCLE_QUIT) {
            if (!x->config->quiet)
                print_pattern(x, x->out);
        }
        if (result == CYCLE_QUIT_SILENT) {
            x->quitting = 1;
            break;
        }
        dump_append(x);
        if (result == CYCLE_RESTART)
            needRead = 0;
        if (result == CYCLE_QUIT) {
            x->quitting = 1;
            break;
        }
    }
    dump_append(x);

    if (x->readerOpen) {
        text_reader_free(&x->reader);
        if (x->currentFile != stdin)
            fclose(x->currentFile);
        x->readerOpen = 0;
    }
}

/* ---- -i ------------------------------------------------------------------------------------ */

typedef struct {
    char native[VPATH_MAX];
    char temporary[VPATH_MAX];
    Output output;
} InPlace;

/** Where the output of an -i goes: a temporary file next to the file. Returns 0, or -1 after a message. */
static int in_place_begin(Executor* x, const char* name, InPlace* ip) {
    DWORD attributes;
    char directory[VPATH_MAX], *slash;

    if (strcmp(name, "-") == 0 || vpath_to_native(name, ip->native, sizeof ip->native) != 0) {
        fprintf(stderr, PROGRAM ": couldn't edit %s: not a regular file\n", name);
        x->exitStatus = 4;
        return -1;
    }
    if (fs_attributes(ip->native, &attributes) != 0) {
        fprintf(stderr, PROGRAM ": can't read %s: No such file or directory\n", name);
        x->exitStatus = 2;
        return -1;
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        fprintf(stderr, PROGRAM ": couldn't edit %s: not a regular file\n", name);
        x->exitStatus = 4;
        return -1;
    }

    snprintf(directory, sizeof directory, "%s", ip->native);
    slash = strrchr(directory, '\\');
    if (slash == NULL)
        slash = strrchr(directory, '/');
    if (slash != NULL)
        *slash = '\0';
    else
        strcpy(directory, ".");
    if (GetTempFileNameA(directory, "sed", 0, ip->temporary) == 0) {
        fprintf(stderr, PROGRAM ": couldn't open temporary file in %s\n", name);
        x->exitStatus = 4;
        return -1;
    }
    memset(&ip->output, 0, sizeof ip->output);
    ip->output.name = ip->temporary;
    ip->output.file = fopen(ip->temporary, "wb");
    if (ip->output.file == NULL) {
        fprintf(stderr, PROGRAM ": couldn't open temporary file %s\n", ip->temporary);
        DeleteFileA(ip->temporary);
        x->exitStatus = 4;
        return -1;
    }
    return 0;
}

/** The name of the backup: the suffix with * for the base name, or the base name followed by the suffix. */
static void backup_name(const char* name, const char* suffix, char* out_, size_t size) {
    char base[VPATH_MAX], pattern[VPATH_MAX], result[VPATH_MAX];
    const char* slash = strrchr(name, '/');
    const char* back = strrchr(name, '\\');
    size_t directoryLength, n = 0;
    const char* p;

    if (back != NULL && (slash == NULL || back > slash))
        slash = back;
    directoryLength = slash != NULL ? (size_t)(slash - name) + 1 : 0;
    snprintf(base, sizeof base, "%s", name + directoryLength);

    if (strchr(suffix, '*') != NULL)
        snprintf(pattern, sizeof pattern, "%s", suffix);
    else
        snprintf(pattern, sizeof pattern, "*%s", suffix);
    for (p = pattern; *p != '\0' && n + 1 < sizeof result; p++) {
        if (*p == '*') {
            size_t k;
            for (k = 0; base[k] != '\0' && n + 1 < sizeof result; k++)
                result[n++] = base[k];
        } else {
            result[n++] = *p;
        }
    }
    result[n] = '\0';

    if (strchr(result, '/') != NULL || strchr(result, '\\') != NULL)
        snprintf(out_, size, "%s", result);
    else
        snprintf(out_, size, "%.*s%s", (int)directoryLength, name, result);
}

static void in_place_end(Executor* x, const char* name, InPlace* ip) {
    fclose(ip->output.file);
    if (x->config->suffix != NULL && x->config->suffix[0] != '\0') {
        char backup[VPATH_MAX], native[VPATH_MAX];
        backup_name(name, x->config->suffix, backup, sizeof backup);
        if (vpath_to_native(backup, native, sizeof native) == 0)
            MoveFileExA(ip->native, native, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
    }
    if (!MoveFileExA(ip->temporary, ip->native, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        fprintf(stderr, PROGRAM ": cannot rename %s: %s\n", ip->temporary, fs_strerror(GetLastError()));
        DeleteFileA(ip->temporary);
        x->exitStatus = 4;
    }
}

int sed_run(Script* s, const SedConfig* c, const char* const* files, size_t fileCount) {
    Executor executor;
    Executor* x = &executor;
    size_t i;

    memset(x, 0, sizeof *x);
    x->hsEnding = 1;
    x->quitStatus = -1;
    x->script = s;
    x->config = c;
    x->terminator = c->terminator;
    x->stdoutOutput.name = "/dev/stdout";
    x->stdoutOutput.file = stdout;
    x->out = &x->stdoutOutput;
    x->allNames = files;
    reset_ranges(x);

    if (!c->separate) {
        run_stream(x, files, fileCount, 0);
    } else {
        for (i = 0; i < fileCount && !x->quitting; i++) {
            InPlace ip;
            int editing = 0;

            if (c->inPlace) {
                if (in_place_begin(x, files[i], &ip) != 0)
                    continue;
                x->out = &ip.output;
                editing = 1;
            }
            x->lineNumber = 0;
            reset_ranges(x);
            run_stream(x, files + i, 1, i);
            if (editing) {
                in_place_end(x, files[i], &ip);
                x->out = &x->stdoutOutput;
            }
        }
    }

    {
        Output* o;
        for (o = x->script->outputs; o != NULL; o = o->next)
            if (o->file != stdout && o->file != stderr)
                fclose(o->file);
    }
    if (fflush(stdout) != 0 && x->exitStatus == 0) {
        fprintf(stderr, PROGRAM ": couldn't flush stdout\n");
        x->exitStatus = 4;
    }
    return x->quitStatus >= 0 ? x->quitStatus : x->exitStatus;
}
