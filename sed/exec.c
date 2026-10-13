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

static Output outStdout = { "/dev/stdout", NULL, 0, NULL };
static Output* outputs;                 // the files of the w commands

static const SedConfig* config;
static Script* script;
static int terminator;

static const char* const* inputFiles;
static size_t inputCount, nextInput;
static FILE* currentFile;
static LineReader reader;
static int readerOpen;
static size_t readerIndex;
static const char* const* allNames;     // for F: the names of all the files
static Line look;

static Buf ps, hs;
static int psEnding;
static int hsEnding = 1;
static size_t psFile;
static size_t streamBase;               // the index in the list of all files of the first one of the stream
static long lineNumber;
static int replaced;
static Regex* lastRegex;
static RegexInput regexInput;
static Output* out;
static Append* appends;
static size_t appendCount, appendCapacity;
static int exitStatus;
static int quitStatus = -1;
static int quitting;

/* ---- output -------------------------------------------------------------------------------- */

Output* sed_output_open(const char* name) {
    Output* o;
    char native[VPATH_MAX];

    for (o = outputs; o != NULL; o = o->next)
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
    o->next = outputs;
    outputs = o;
    return o;
}

static void put_missing_newline(Output* o) {
    if (o->missingNewline) {
        fputc(terminator, o->file);
        o->missingNewline = 0;
    }
}

static void finish_write(Output* o) {
    if (config->unbuffered || o->file == stderr)
        fflush(o->file);
}

static void write_raw(Output* o, const void* data, size_t length) {
    put_missing_newline(o);
    fwrite(data, 1, length, o->file);
    finish_write(o);
}

/** A line with its ending: 0 none (nothing follows, until more is written), 1 the terminator, 2 CR LF. */
static void write_line(Output* o, const void* data, size_t length, int ending) {
    put_missing_newline(o);
    fwrite(data, 1, length, o->file);
    if (ending == 2)
        fputs("\r\n", o->file);
    else if (ending == 1)
        fputc(terminator, o->file);
    else
        o->missingNewline = 1;
    finish_write(o);
}

static void print_pattern(Output* o) {
    write_line(o, ps.data, ps.length, psEnding);
}

static void queue_append(int kind, const void* data, size_t length) {
    Append* a;
    if (VEC_RESERVE(appends, appendCapacity, appendCount + 1) != 0)
        return;
    a = &appends[appendCount++];
    a->kind = kind;
    a->data = malloc(length + 1);
    if (a->data == NULL) {
        appendCount--;
        return;
    }
    memcpy(a->data, data, length);
    a->data[length] = '\0';
    a->length = length;
}

static void dump_append(void) {
    size_t i;
    for (i = 0; i < appendCount; i++) {
        Append* a = &appends[i];
        if (a->kind == 't') {
            write_raw(out, a->data, a->length);
        } else {
            char native[VPATH_MAX];
            if (vpath_to_native((const char*)a->data, native, sizeof native) == 0) {
                FILE* f = fopen(native, "rb");
                if (f != NULL) {
                    unsigned char chunk[8192];
                    size_t n;
                    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
                        write_raw(out, chunk, n);
                    fclose(f);
                }
            }
        }
        free(a->data);
    }
    appendCount = 0;
}

/* ---- input --------------------------------------------------------------------------------- */

/** Opens an input file; a failure is reported and sets the exit status. */
static int open_input(const char* name, FILE** file) {
    char native[VPATH_MAX];
    const char* reason;
    DWORD attributes;

    if (strcmp(name, "-") == 0) {
        *file = stdin;
        return 0;
    }
    if (vpath_to_native(name, native, sizeof native) == VPATH_ROOT) {
        fprintf(stderr, PROGRAM ": couldn't edit %s: not a regular file\n", name);
        exitStatus = config->inPlace ? 4 : 2;
        return -1;
    }
    if (vpath_to_native(name, native, sizeof native) != 0) {
        reason = strerror(errno);
    } else if (fs_attributes(native, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        if (config->inPlace)
            fprintf(stderr, PROGRAM ": couldn't edit %s: not a regular file\n", name);
        else
            fprintf(stderr, PROGRAM ": read error on %s: Is a directory\n", name);
        exitStatus = config->inPlace ? 4 : 2;
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
    exitStatus = 2;
    return -1;
}

/** The next line of the input files of the stream, into line. Returns 0 at the end. */
static int fetch(Line* line) {
    for (;;) {
        TextLine text;

        if (!readerOpen) {
            if (nextInput >= inputCount)
                return 0;
            readerIndex = nextInput;
            if (open_input(inputFiles[nextInput++], &currentFile) != 0)
                continue;
            text_reader_init(&reader, currentFile, terminator);
            readerOpen = 1;
        }
        if (text_reader_next(&reader, &text) > 0) {
            line->text.length = 0;
            buf_add(&line->text, text.content, text.length);
            line->ending = text.ending;
            line->file = readerIndex;
            line->valid = 1;
            return 1;
        }
        text_reader_free(&reader);
        if (currentFile != stdin)
            fclose(currentFile);
        readerOpen = 0;
    }
}

static int has_next(void) {
    if (look.valid)
        return 1;
    return fetch(&look);
}

/** Reads the next line into the pattern space (after it, with a terminator between, when append). */
static int read_pattern_space(int append) {
    dump_append();
    if (!look.valid && !fetch(&look))
        return 0;
    look.valid = 0;

    if (append)
        buf_add_byte(&ps, terminator);
    else
        ps.length = 0;
    buf_add(&ps, look.text.data, look.text.length);
    psEnding = look.ending;
    psFile = look.file;
    lineNumber++;
    replaced = 0;
    return 1;
}

/* ---- addresses ----------------------------------------------------------------------------- */

static int load_input(void) {
    return regex_input_set(&regexInput, ps.data, ps.length);
}

static int regex_matches(Regex* re) {
    size_t start, end;

    if (re == NULL)
        re = lastRegex;
    if (re == NULL) {
        fprintf(stderr, PROGRAM ": no previous regular expression\n");
        exit(1);
    }
    lastRegex = re;
    if (load_input() != 0)
        return 0;
    return regex_search(re, &regexInput, 0, &start, &end);
}

static int is_last_line(void) {
    return !has_next();
}

static int address_matches(const Address* a) {
    switch (a->kind) {
        case ADDR_LINE:
            return lineNumber == a->number;
        case ADDR_LAST:
            return is_last_line();
        case ADDR_REGEX:
            return regex_matches(a->regex);
        case ADDR_STEP:
            if (a->step <= 0)
                return lineNumber == a->number;
            return lineNumber >= a->number && (lineNumber - a->number) % a->step == 0;
        default:
            return 0;
    }
}

static int command_matches(Command* c) {
    int matched;

    if (c->first.kind == ADDR_NONE)
        return !c->negate;

    if (c->second.kind == ADDR_NONE) {
        matched = address_matches(&c->first);
    } else if (c->rangeActive) {
        matched = 1;
        switch (c->second.kind) {
            case ADDR_LINE:
            case ADDR_PLUS:
            case ADDR_MULTIPLE:
                if (lineNumber >= c->rangeEnd) {
                    c->rangeActive = 0;
                    matched = lineNumber == c->rangeEnd;
                }
                break;
            default:
                if (address_matches(&c->second))
                    c->rangeActive = 0;
                break;
        }
    } else {
        matched = address_matches(&c->first);
        if (c->first.kind == ADDR_ZERO)
            matched = 0;
        if (matched) {
            switch (c->second.kind) {
                case ADDR_LINE:
                    if (c->second.number > lineNumber) {
                        c->rangeActive = 1;
                        c->rangeEnd = c->second.number;
                    }
                    break;
                case ADDR_PLUS:
                    if (c->second.number > 0) {
                        c->rangeActive = 1;
                        c->rangeEnd = lineNumber + c->second.number;
                    }
                    break;
                case ADDR_MULTIPLE:
                    if (c->second.number > 0 && lineNumber % c->second.number != 0) {
                        c->rangeActive = 1;
                        c->rangeEnd = (lineNumber / c->second.number + 1) * c->second.number;
                    }
                    break;
                case ADDR_LAST:
                    c->rangeActive = !is_last_line();
                    break;
                default:
                    c->rangeActive = 1;     // a regular expression is looked for from the next line on
                    break;
            }
        }
    }
    return matched != c->negate;
}

static void reset_ranges(void) {
    size_t i;
    for (i = 0; i < script->count; i++) {
        Command* c = &script->commands[i];
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

/** s: returns 1 when something was replaced. */
static int substitute(const Substitution* sub) {
    Regex* re = sub->regex != NULL ? sub->regex : lastRegex;
    Buf result;
    size_t* spans = NULL;
    size_t position = 0, copied = 0, previousEnd = (size_t)-1, start, end;
    long count = 0;
    int didReplace = 0;

    if (re == NULL) {
        fprintf(stderr, PROGRAM ": no previous regular expression\n");
        exit(1);
    }
    lastRegex = re;
    if (load_input() != 0)
        return 0;
    if (sub->needGroups) {
        spans = malloc((regex_group_count(re) + 1) * 2 * sizeof *spans);
        if (spans == NULL)
            return 0;
    }
    memset(&result, 0, sizeof result);

    while (position <= regexInput.length && regex_search(re, &regexInput, position, &start, &end)) {
        if (start == end && start == previousEnd) {
            // an empty match right after the previous one does not count
            if (start >= regexInput.length)
                break;
            position = start + 1;
            continue;
        }
        count++;
        if (count >= sub->number) {
            CaseState state = { 0, 0 };
            size_t k;

            buf_add(&result, ps.data + regexInput.offset[copied], regexInput.offset[start] - regexInput.offset[copied]);
            if (spans != NULL)
                regex_groups(re, &regexInput, start, end, spans);
            for (k = 0; k < sub->partCount; k++) {
                const Part* part = &sub->parts[k];
                if (part->kind == PART_TEXT) {
                    add_with_case(&result, part->text, part->length, &state);
                } else if (part->kind == PART_GROUP) {
                    size_t from = start, to = end;
                    if (part->number > 0) {
                        from = spans != NULL ? spans[2 * part->number] : (size_t)-1;
                        to = spans != NULL ? spans[2 * part->number + 1] : (size_t)-1;
                    }
                    if (from != (size_t)-1 && to != (size_t)-1 && from <= to)
                        add_with_case(&result, ps.data + regexInput.offset[from],
                                      regexInput.offset[to] - regexInput.offset[from], &state);
                } else if (part->number == 'E') {
                    state.mode = 0;
                    state.once = 0;
                } else if (part->number == 'U' || part->number == 'L') {
                    state.mode = part->number;
                } else {
                    state.once = part->number;
                }
            }
            copied = end;
            didReplace = 1;
            if (!sub->global)
                break;
        }
        previousEnd = end;
        if (start == end) {
            if (end >= regexInput.length)
                break;
            position = end + 1;
        } else {
            position = end;
        }
    }

    if (didReplace) {
        buf_add(&result, ps.data + regexInput.offset[copied], ps.length - regexInput.offset[copied]);
        free(ps.data);
        ps = result;
    } else {
        free(result.data);
    }
    free(spans);
    return didReplace;
}

static void translate(const Command* c) {
    Buf result;
    size_t i = 0, k;

    memset(&result, 0, sizeof result);
    while (i < ps.length) {
        size_t n;
        uint32_t ch = utf8_decode(ps.data + i, ps.length - i, &n);
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
            buf_add(&result, ps.data + i, n);
        i += n;
    }
    free(ps.data);
    ps = result;
}

/** l: the pattern space with the characters that do not show written as escapes, wrapped. */
static void list_pattern(long width) {
    size_t i;
    long column = 0;
    char piece[8];

    if (width <= 1)
        width = 0;
    for (i = 0; i < ps.length; i++) {
        unsigned char c = ps.data[i];
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
            write_raw(out, "\\\n", 2);
            column = 0;
        }
        write_raw(out, piece, length);
        column += (long)length;
    }
    write_raw(out, "$\n", 2);
}

static struct Reader* reader_for(Command* c) {
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
                text_reader_init(&r->lines, r->file, terminator);
                r->open = 1;
            }
        }
    }
    return r;
}

static size_t find_terminator(void) {
    size_t i;
    for (i = 0; i < ps.length; i++)
        if (ps.data[i] == terminator)
            return i;
    return (size_t)-1;
}

static CycleResult execute_program(void) {
    size_t pc = 0;

    while (pc < script->count) {
        Command* c = &script->commands[pc];

        if (!command_matches(c)) {
            pc = c->name == '{' ? c->target + 1 : pc + 1;
            continue;
        }

        switch (c->name) {
            case '{': case '}': case ':': case 'v':
                break;
            case '=': {
                char number[32];
                int n = sprintf(number, "%ld\n", lineNumber);
                write_raw(out, number, (size_t)n);
                break;
            }
            case 'a':
                queue_append('t', c->text, c->length);
                break;
            case 'i':
                write_raw(out, c->text, c->length);
                break;
            case 'c':
                if (c->second.kind == ADDR_NONE || c->negate || !c->rangeActive)
                    write_raw(out, c->text, c->length);
                ps.length = 0;
                return CYCLE_DELETE;
            case 'd':
                ps.length = 0;
                return CYCLE_DELETE;
            case 'D': {
                size_t at = find_terminator();
                if (at == (size_t)-1) {
                    ps.length = 0;
                    return CYCLE_DELETE;
                }
                memmove(ps.data, ps.data + at + 1, ps.length - at - 1);
                ps.length -= at + 1;
                return CYCLE_RESTART;
            }
            case 'F': {
                const char* name = allNames[streamBase + psFile];
                write_raw(out, name, strlen(name));
                write_raw(out, "\n", 1);
                break;
            }
            // the ending goes with the text: a last line without one gets the one of what it is mixed with
            case 'g':
                ps.length = 0;
                buf_add(&ps, hs.data, hs.length);
                psEnding = hsEnding;
                break;
            case 'G':
                buf_add_byte(&ps, terminator);
                buf_add(&ps, hs.data, hs.length);
                if (hsEnding == 0)
                    psEnding = 0;
                else if (psEnding == 0)
                    psEnding = hsEnding;
                break;
            case 'h':
                hs.length = 0;
                buf_add(&hs, ps.data, ps.length);
                hsEnding = psEnding;
                break;
            case 'H':
                buf_add_byte(&hs, terminator);
                buf_add(&hs, ps.data, ps.length);
                if (psEnding == 0)
                    hsEnding = 0;
                else if (hsEnding == 0)
                    hsEnding = psEnding;
                break;
            case 'x': {
                Buf t = ps;
                int e = psEnding;
                ps = hs;
                hs = t;
                psEnding = hsEnding;
                hsEnding = e;
                break;
            }
            case 'l':
                list_pattern(c->number >= 0 ? c->number : config->lineWrap);
                break;
            case 'n':
                if (!has_next())
                    return CYCLE_END;
                if (!config->quiet)
                    print_pattern(out);
                read_pattern_space(0);
                break;
            case 'N':
                if (!has_next())
                    return CYCLE_END;
                read_pattern_space(1);
                break;
            case 'p':
                print_pattern(out);
                break;
            case 'P': {
                size_t at = find_terminator();
                write_line(out, ps.data, at == (size_t)-1 ? ps.length : at, at == (size_t)-1 ? psEnding : (psEnding == 2 ? 2 : 1));
                break;
            }
            case 'q':
                quitStatus = c->number >= 0 ? (int)c->number : 0;
                return CYCLE_QUIT;
            case 'Q':
                quitStatus = c->number >= 0 ? (int)c->number : 0;
                return CYCLE_QUIT_SILENT;
            case 'r':
                queue_append('r', c->text, c->length);
                break;
            case 'R': {
                struct Reader* r = reader_for(c);
                TextLine line;
                if (r != NULL && r->open && text_reader_next(&r->lines, &line) > 0) {
                    Buf text;
                    memset(&text, 0, sizeof text);
                    buf_add(&text, line.content, line.length);
                    buf_add_byte(&text, '\n');
                    queue_append('t', text.data, text.length);
                    free(text.data);
                }
                break;
            }
            case 's':
                if (substitute(c->sub)) {
                    replaced = 1;
                    if (c->sub->print)
                        print_pattern(out);
                    if (c->sub->out != NULL)
                        print_pattern(c->sub->out);
                }
                break;
            case 't':
                if (replaced) {
                    replaced = 0;
                    pc = c->target;
                    continue;
                }
                break;
            case 'T':
                if (!replaced) {
                    pc = c->target;
                    continue;
                }
                replaced = 0;
                break;
            case 'b':
                pc = c->target;
                continue;
            case 'w':
                print_pattern(c->out);
                break;
            case 'W': {
                size_t at = find_terminator();
                write_line(c->out, ps.data, at == (size_t)-1 ? ps.length : at, at == (size_t)-1 ? psEnding : (psEnding == 2 ? 2 : 1));
                break;
            }
            case 'y':
                translate(c);
                break;
            case 'z':
                ps.length = 0;
                break;
            default:
                break;
        }
        pc++;
    }
    return CYCLE_END;
}

/* ---- the streams --------------------------------------------------------------------------- */

static void run_stream(const char* const* files, size_t count, size_t firstIndex) {
    int needRead = 1;

    inputFiles = files;
    inputCount = count;
    nextInput = 0;
    readerOpen = 0;
    look.valid = 0;
    streamBase = firstIndex;

    for (;;) {
        CycleResult result;

        if (needRead && !read_pattern_space(0))
            break;
        needRead = 1;

        result = execute_program();
        if (result == CYCLE_QUIT && psEnding == 0)
            psEnding = 1;               // like GNU sed, q ends the last line even when it had no ending
        if (result == CYCLE_END || result == CYCLE_QUIT) {
            if (!config->quiet)
                print_pattern(out);
        }
        if (result == CYCLE_QUIT_SILENT) {
            quitting = 1;
            break;
        }
        dump_append();
        if (result == CYCLE_RESTART)
            needRead = 0;
        if (result == CYCLE_QUIT) {
            quitting = 1;
            break;
        }
    }
    dump_append();

    if (readerOpen) {
        text_reader_free(&reader);
        if (currentFile != stdin)
            fclose(currentFile);
        readerOpen = 0;
    }
}

/* ---- -i ------------------------------------------------------------------------------------ */

typedef struct {
    char native[VPATH_MAX];
    char temporary[VPATH_MAX];
    Output output;
} InPlace;

/** Where the output of an -i goes: a temporary file next to the file. Returns 0, or -1 after a message. */
static int in_place_begin(const char* name, InPlace* ip) {
    DWORD attributes;
    char directory[VPATH_MAX], *slash;

    if (strcmp(name, "-") == 0 || vpath_to_native(name, ip->native, sizeof ip->native) != 0) {
        fprintf(stderr, PROGRAM ": couldn't edit %s: not a regular file\n", name);
        exitStatus = 4;
        return -1;
    }
    if (fs_attributes(ip->native, &attributes) != 0) {
        fprintf(stderr, PROGRAM ": can't read %s: No such file or directory\n", name);
        exitStatus = 2;
        return -1;
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        fprintf(stderr, PROGRAM ": couldn't edit %s: not a regular file\n", name);
        exitStatus = 4;
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
        exitStatus = 4;
        return -1;
    }
    memset(&ip->output, 0, sizeof ip->output);
    ip->output.name = ip->temporary;
    ip->output.file = fopen(ip->temporary, "wb");
    if (ip->output.file == NULL) {
        fprintf(stderr, PROGRAM ": couldn't open temporary file %s\n", ip->temporary);
        DeleteFileA(ip->temporary);
        exitStatus = 4;
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

static void in_place_end(const char* name, InPlace* ip) {
    fclose(ip->output.file);
    if (config->suffix != NULL && config->suffix[0] != '\0') {
        char backup[VPATH_MAX], native[VPATH_MAX];
        backup_name(name, config->suffix, backup, sizeof backup);
        if (vpath_to_native(backup, native, sizeof native) == 0)
            MoveFileExA(ip->native, native, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
    }
    if (!MoveFileExA(ip->temporary, ip->native, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        fprintf(stderr, PROGRAM ": cannot rename %s: %s\n", ip->temporary, fs_strerror(GetLastError()));
        DeleteFileA(ip->temporary);
        exitStatus = 4;
    }
}

int sed_run(Script* s, const SedConfig* c, const char* const* files, size_t fileCount) {
    size_t i;

    script = s;
    config = c;
    terminator = c->terminator;
    outStdout.file = stdout;
    out = &outStdout;
    allNames = files;
    reset_ranges();

    if (!c->separate) {
        run_stream(files, fileCount, 0);
    } else {
        for (i = 0; i < fileCount && !quitting; i++) {
            InPlace ip;
            int editing = 0;

            if (c->inPlace) {
                if (in_place_begin(files[i], &ip) != 0)
                    continue;
                out = &ip.output;
                editing = 1;
            }
            lineNumber = 0;
            reset_ranges();
            run_stream(files + i, 1, i);
            if (editing) {
                in_place_end(files[i], &ip);
                out = &outStdout;
            }
        }
    }

    {
        Output* o;
        for (o = outputs; o != NULL; o = o->next)
            if (o->file != stdout && o->file != stderr)
                fclose(o->file);
    }
    if (fflush(stdout) != 0 && exitStatus == 0) {
        fprintf(stderr, PROGRAM ": couldn't flush stdout\n");
        exitStatus = 4;
    }
    return quitStatus >= 0 ? quitStatus : exitStatus;
}
