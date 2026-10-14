/*
 * Parsing the script of sed: addresses, commands, the arguments of s and y, and jumps.
 *
 * The chunks (-e options and -f files) are joined with line feeds into one text that is parsed in one
 * go, so that a\ followed by the text in the next -e works. A message names the chunk and the place in it.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sed.h"
#include "utf8.h"

typedef struct {
    const unsigned char* text;
    size_t               length, pos;
    const ScriptChunk*   chunks;
    size_t               chunkCount;
    size_t*              chunkStart;        // where each chunk starts in text
    const SedConfig*     config;
    Script*              script;
    size_t               capacity;
    size_t*              blocks;            // the open {
    size_t               blockCount, blockCapacity;
    char*                error;
    size_t               errorSize;
    int                  panic;             // the message is of the kind that ends with status 4
} Parser;

/* ---- messages ------------------------------------------------------------------------------ */

static int fail(Parser* p, const char* format, ...) {
    char message[200];
    size_t i = 0, line = 1, k;
    va_list args;

    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);

    while (i + 1 < p->chunkCount && p->chunkStart[i + 1] <= p->pos)
        i++;
    if (p->chunks[i].file != NULL) {
        for (k = p->chunkStart[i]; k < p->pos && k < p->length; k++)
            if (p->text[k] == '\n')
                line++;
        snprintf(p->error, p->errorSize, "file %s line %zu: %s", p->chunks[i].file, line, message);
    } else {
        snprintf(p->error, p->errorSize, "-e expression #%d, char %zu: %s", p->chunks[i].number,
                 p->pos - p->chunkStart[i], message);
    }
    return -1;
}

/* ---- the text ------------------------------------------------------------------------------ */

static int at_end(const Parser* p) {
    return p->pos >= p->length;
}

static int peek(const Parser* p) {
    return at_end(p) ? -1 : p->text[p->pos];
}

static void skip_blanks(Parser* p) {
    while (!at_end(p) && (p->text[p->pos] == ' ' || p->text[p->pos] == '\t'))
        p->pos++;
}

static void skip_space(Parser* p) {
    while (!at_end(p) && isspace(p->text[p->pos]))
        p->pos++;
}

/** After the bracket expression that starts at i: its end, or 0 when it is not closed. */
static size_t bracket_end(const unsigned char* t, size_t length, size_t i) {
    i++;
    if (i < length && t[i] == '^')
        i++;
    if (i < length && t[i] == ']')
        i++;
    while (i < length) {
        if (t[i] == '[' && i + 1 < length && (t[i + 1] == ':' || t[i + 1] == '.' || t[i + 1] == '=')) {
            unsigned char kind = t[i + 1];
            size_t k = i + 2;
            while (k + 1 < length && !(t[k] == kind && t[k + 1] == ']'))
                k++;
            if (k + 1 >= length)
                return 0;
            i = k + 2;
            continue;
        }
        if (t[i] == ']')
            return i + 1;
        if (t[i] == '\n')
            return 0;
        i++;
    }
    return 0;
}

/**
 * Reads up to the delimiter, which is the next character that is not escaped (and, for a regular
 * expression, not in a bracket expression). \delimiter becomes the delimiter, the other escapes
 * stay as they are. Returns 0, or -1 when the delimiter is not found.
 */
static int read_delimited(Parser* p, int delimiter, int isRegex, Buf* out) {
    while (!at_end(p)) {
        int c = p->text[p->pos++];

        if (c == '\\') {
            int next;
            if (at_end(p))
                return -1;
            next = p->text[p->pos++];
            if (next == delimiter) {
                buf_add_byte(out, next);
            } else {
                buf_add_byte(out, '\\');
                buf_add_byte(out, next);
            }
        } else if (c == delimiter) {
            return 0;
        } else if (c == '\n' && isRegex) {
            return -1;
        } else if (isRegex && c == '[') {
            size_t end = bracket_end(p->text, p->length, p->pos - 1);
            if (end != 0 && memchr(p->text + p->pos - 1, delimiter, end - (p->pos - 1)) != NULL) {
                // the delimiter inside the brackets belongs to them
                buf_add(out, p->text + p->pos - 1, end - (p->pos - 1));
                p->pos = end;
            } else {
                buf_add_byte(out, c);
            }
        } else {
            buf_add_byte(out, c);
        }
    }
    return -1;
}

/** \xHH, \oOOO and \dDDD at s[*i] (after the backslash). Returns the value, or -1 when it is none. */
static int numeric_escape(const unsigned char* s, size_t length, size_t* i) {
    int base, digits, value = 0, count = 0;
    size_t k = *i + 1;

    switch (s[*i]) {
        case 'x': base = 16; digits = 2; break;
        case 'o': base = 8; digits = 3; break;
        case 'd': base = 10; digits = 3; break;
        default: return -1;
    }
    while (count < digits && k < length) {
        int c = s[k], d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        value = value * base + d;
        k++;
        count++;
    }
    if (count == 0)
        return -1;
    *i = k - 1;
    return value & 0xFF;
}

/* ---- regular expressions ------------------------------------------------------------------- */

/** The pattern as the engine takes it: \n and \t (and the numeric escapes) are the characters. */
static int prepare_regex(const unsigned char* raw, size_t length, Buf* out) {
    size_t i;
    for (i = 0; i < length; i++) {
        if (raw[i] == '\\' && i + 1 < length) {
            int value;
            i++;
            if (raw[i] == 'n') {
                buf_add_byte(out, '\n');
            } else if (raw[i] == 't') {
                buf_add_byte(out, '\t');
            } else if ((value = numeric_escape(raw, length, &i)) >= 0) {
                // a special character that came from an escape stays that character
                if (strchr("\\.*[]^$+?(){}|", value) != NULL)
                    buf_add_byte(out, '\\');
                buf_add_byte(out, value);
            } else {
                buf_add_byte(out, '\\');
                buf_add_byte(out, raw[i]);
            }
        } else {
            buf_add_byte(out, raw[i]);
        }
    }
    return buf_add_byte(out, 0);        // the terminator is not counted
}

/** Compiles the expression; an empty one gives NULL (the last one used). Returns 0 or -1 after a message. */
static int compile_regex(Parser* p, const Buf* raw, int icase, Regex** out) {
    Buf prepared;
    char error[200];
    const char* patterns[1];
    int flags = REGEX_GROUPS;

    *out = NULL;
    if (raw->length == 0)
        return 0;

    memset(&prepared, 0, sizeof prepared);
    prepare_regex(raw->data, raw->length, &prepared);
    patterns[0] = (const char*)prepared.data;
    if (p->config->extended)
        flags |= REGEX_EXTENDED;
    if (icase)
        flags |= REGEX_ICASE;
    *out = regex_compile(patterns, 1, flags, error, sizeof error);
    free(prepared.data);
    if (*out == NULL)
        return fail(p, "%s", error);
    return 0;
}

/* ---- addresses ----------------------------------------------------------------------------- */

static int read_number(Parser* p, long* out) {
    long value = 0;
    if (at_end(p) || !isdigit(p->text[p->pos]))
        return -1;
    while (!at_end(p) && isdigit(p->text[p->pos])) {
        value = value * 10 + (p->text[p->pos] - '0');
        p->pos++;
    }
    *out = value;
    return 0;
}

/** A regular expression address: /re/ or \cREc, with the flags I and M. */
static int parse_regex_address(Parser* p, Address* address) {
    int delimiter;
    Buf raw;
    int icase = 0, status;

    memset(&raw, 0, sizeof raw);
    if (peek(p) == '\\') {
        p->pos++;
        if (at_end(p))
            return fail(p, "unexpected `,'");
    }
    delimiter = p->text[p->pos++];
    if (read_delimited(p, delimiter, 1, &raw) != 0)
        return fail(p, "unterminated address regex");

    for (;;) {
        int c = peek(p);
        if (c == 'I') {
            icase = 1;
            p->pos++;
        } else if (c == 'M') {
            return fail(p, "the M flag is not supported");
        } else {
            break;
        }
    }
    address->kind = ADDR_REGEX;
    status = compile_regex(p, &raw, icase, &address->regex);
    free(raw.data);
    return status;
}

static int parse_first_address(Parser* p, Address* address) {
    int c = peek(p);
    long number;

    if (c == '/' || c == '\\')
        return parse_regex_address(p, address);
    if (c == '$') {
        p->pos++;
        address->kind = ADDR_LAST;
        return 0;
    }
    if (c >= '0' && c <= '9') {
        read_number(p, &number);
        if (peek(p) == '~') {
            long step = 0;
            p->pos++;
            read_number(p, &step);
            address->kind = ADDR_STEP;
            address->number = number;
            address->step = step;
        } else {
            address->kind = number == 0 ? ADDR_ZERO : ADDR_LINE;
            address->number = number;
        }
    }
    return 0;
}

static int parse_second_address(Parser* p, Address* address) {
    int c = peek(p);
    long number;

    if (c == '+' || c == '~') {
        p->pos++;
        if (read_number(p, &number) != 0)
            return fail(p, "expected a number after `%c'", c);
        address->kind = c == '+' ? ADDR_PLUS : ADDR_MULTIPLE;
        address->number = number;
        return 0;
    }
    if (c == '/' || c == '\\')
        return parse_regex_address(p, address);
    if (c == '$') {
        p->pos++;
        address->kind = ADDR_LAST;
        return 0;
    }
    if (c >= '0' && c <= '9') {
        read_number(p, &number);
        address->kind = ADDR_LINE;
        address->number = number;
        return 0;
    }
    return fail(p, "unexpected `,'");
}

/* ---- the arguments of commands ------------------------------------------------------------- */

/** After a command: only blanks, then ; or a line feed or } or # or the end. */
static int read_end(Parser* p) {
    skip_blanks(p);
    if (at_end(p))
        return 0;
    switch (p->text[p->pos]) {
        case ';':
        case '\n':
            p->pos++;
            return 0;
        case '}':
        case '#':
            return 0;
        default:
            return fail(p, "extra characters after command");
    }
}

/** The text of a, i and c. */
static int read_text(Parser* p, Command* cmd) {
    Buf text;

    memset(&text, 0, sizeof text);
    skip_blanks(p);
    if (at_end(p))
        return fail(p, "expected \\ after `a', `c' or `i'");

    if (peek(p) == '\\') {
        p->pos++;
        skip_blanks(p);
        if (peek(p) == '\n')
            p->pos++;
    } else if (peek(p) == '\n') {
        return fail(p, "expected \\ after `a', `c' or `i'");
    }

    while (!at_end(p)) {
        int c = p->text[p->pos++];
        if (c == '\\') {
            if (at_end(p))
                break;
            c = p->text[p->pos++];
        } else if (c == '\n') {
            break;
        }
        buf_add_byte(&text, c);
    }
    buf_add_byte(&text, '\n');
    cmd->text = text.data;
    cmd->length = text.length;
    return 0;
}

/** A label or a file name: to the end of the line (name) or to a blank or ; (label). */
static int read_word(Parser* p, Command* cmd, int toLineEnd) {
    size_t start;
    unsigned char* copy;

    skip_blanks(p);
    start = p->pos;
    while (!at_end(p)) {
        int c = p->text[p->pos];
        if (c == '\n')
            break;
        if (!toLineEnd && (isspace(c) || c == ';' || c == '}'))
            break;
        p->pos++;
    }
    copy = malloc(p->pos - start + 1);
    if (copy == NULL)
        return fail(p, "memory exhausted");
    memcpy(copy, p->text + start, p->pos - start);
    copy[p->pos - start] = '\0';
    cmd->text = copy;
    cmd->length = p->pos - start;
    return 0;
}

static int parse_replacement(Parser* p, const Buf* raw, Substitution* sub) {
    Buf literal;
    size_t i, capacity = 0;

    memset(&literal, 0, sizeof literal);
    sub->parts = NULL;
    sub->partCount = 0;

#define FLUSH_LITERAL()                                                                     \
    do {                                                                                    \
        if (literal.length > 0) {                                                           \
            if (VEC_RESERVE(sub->parts, capacity, sub->partCount + 1) != 0)                 \
                return fail(p, "memory exhausted");                                         \
            sub->parts[sub->partCount].kind = PART_TEXT;                                    \
            sub->parts[sub->partCount].text = literal.data;                                 \
            sub->parts[sub->partCount].length = literal.length;                             \
            sub->parts[sub->partCount].number = 0;                                          \
            sub->partCount++;                                                               \
            memset(&literal, 0, sizeof literal);                                            \
        }                                                                                   \
    } while (0)

#define ADD_PART(partKind, value)                                                           \
    do {                                                                                    \
        FLUSH_LITERAL();                                                                    \
        if (VEC_RESERVE(sub->parts, capacity, sub->partCount + 1) != 0)                     \
            return fail(p, "memory exhausted");                                             \
        sub->parts[sub->partCount].kind = (partKind);                                       \
        sub->parts[sub->partCount].text = NULL;                                             \
        sub->parts[sub->partCount].length = 0;                                              \
        sub->parts[sub->partCount].number = (value);                                        \
        sub->partCount++;                                                                   \
    } while (0)

    for (i = 0; i < raw->length; i++) {
        int c = raw->data[i];

        if (c == '&') {
            ADD_PART(PART_GROUP, 0);
        } else if (c == '\\' && i + 1 < raw->length) {
            int value;
            c = raw->data[++i];
            if (c >= '0' && c <= '9') {
                ADD_PART(PART_GROUP, c - '0');
                if (c != '0')
                    sub->needGroups = 1;
            } else if (c == 'L' || c == 'U' || c == 'E' || c == 'l' || c == 'u') {
                ADD_PART(PART_CASE, c);
            } else if (c == 'n') {
                buf_add_byte(&literal, '\n');
            } else if (c == 't') {
                buf_add_byte(&literal, '\t');
            } else if (c == 'a') {
                buf_add_byte(&literal, 7);
            } else if (c == 'f') {
                buf_add_byte(&literal, 12);
            } else if (c == 'v') {
                buf_add_byte(&literal, 11);
            } else if (c == 'r') {
                buf_add_byte(&literal, 13);
            } else if (c == 'c' && i + 1 < raw->length) {
                buf_add_byte(&literal, toupper(raw->data[++i]) ^ 0x40);
            } else if ((value = numeric_escape(raw->data, raw->length, &i)) >= 0) {
                buf_add_byte(&literal, value);
            } else {
                buf_add_byte(&literal, c);
            }
        } else {
            buf_add_byte(&literal, c);
        }
    }
    FLUSH_LITERAL();
#undef FLUSH_LITERAL
#undef ADD_PART
    return 0;
}

/** The flags after the s command: g p N i I w FILE. Returns 0, or -1 after a message. */
static int parse_substitute_flags(Parser* p, Substitution* sub, int* icase) {
    long number = 0;
    int numberSeen = 0;

    for (;;) {
        int c = peek(p);
        if (c == 'g') {
            if (sub->global)
                return fail(p, "multiple `g' options to `s' command");
            sub->global = 1;
            p->pos++;
        } else if (c == 'p') {
            if (sub->print)
                return fail(p, "multiple `p' options to `s' command");
            sub->print = 1;
            p->pos++;
        } else if (c == 'i' || c == 'I') {
            *icase = 1;
            p->pos++;
        } else if (c == 'm' || c == 'M') {
            return fail(p, "the M flag is not supported");
        } else if (c == 'e') {
            return fail(p, "the e flag is not supported");
        } else if (c >= '0' && c <= '9') {
            if (numberSeen)
                return fail(p, "multiple number options to `s' command");
            read_number(p, &number);
            if (number == 0)
                return fail(p, "number option to `s' command may not be zero");
            sub->number = number;
            numberSeen = 1;
        } else if (c == 'w') {
            Command file;
            memset(&file, 0, sizeof file);
            p->pos++;
            if (read_word(p, &file, 1) != 0)
                return -1;
            if (file.length == 0)
                return fail(p, "missing filename in r/R/w/W commands");
            sub->out = sed_output_open(p->script, (const char*)file.text);
            if (sub->out == NULL)
                return fail(p, "couldn't open file %s", (const char*)file.text);
            break;
        } else {
            break;
        }
    }
    return 0;
}

static int parse_substitute(Parser* p, Command* cmd) {
    Buf pattern, replacement;
    Substitution* sub;
    int delimiter, icase = 0, status = 0;

    memset(&pattern, 0, sizeof pattern);
    memset(&replacement, 0, sizeof replacement);
    sub = calloc(1, sizeof *sub);
    if (sub == NULL)
        return fail(p, "memory exhausted");
    sub->number = 1;
    cmd->sub = sub;

    if (at_end(p) || peek(p) == '\n' || peek(p) == '\\')
        return fail(p, "unterminated `s' command");
    delimiter = p->text[p->pos++];
    if (read_delimited(p, delimiter, 1, &pattern) != 0 || read_delimited(p, delimiter, 0, &replacement) != 0) {
        free(pattern.data);
        free(replacement.data);
        return fail(p, "unterminated `s' command");
    }

    if (parse_substitute_flags(p, sub, &icase) != 0) {
        free(pattern.data);
        free(replacement.data);
        return -1;
    }

    status = compile_regex(p, &pattern, icase, &sub->regex);
    free(pattern.data);
    if (status == 0)
        status = parse_replacement(p, &replacement, sub);
    free(replacement.data);
    if (status != 0)
        return -1;

    if (sub->regex != NULL) {
        size_t i, groups = regex_group_count(sub->regex);
        for (i = 0; i < sub->partCount; i++)
            if (sub->parts[i].kind == PART_GROUP && (size_t)sub->parts[i].number > groups)
                return fail(p, "invalid reference \\%d on `s' command's RHS", sub->parts[i].number);
    }
    return read_end(p);
}

/** The text of y with its escapes, as code points. */
static int y_string(Parser* p, const Buf* raw, uint32_t** out, size_t* count) {
    uint32_t* chars = malloc((raw->length + 1) * sizeof *chars);
    size_t i = 0, n = 0;

    if (chars == NULL)
        return fail(p, "memory exhausted");
    while (i < raw->length) {
        size_t length;
        if (raw->data[i] == '\\' && i + 1 < raw->length) {
            int c = raw->data[i + 1];
            if (c == '\\') {
                chars[n++] = '\\';
            } else if (c == 'n') {
                chars[n++] = '\n';
            } else if (c == 't') {
                chars[n++] = '\t';
            } else {
                free(chars);
                return fail(p, "unknown command: `%c'", c);
            }
            i += 2;
            continue;
        }
        chars[n++] = utf8_decode(raw->data + i, raw->length - i, &length);
        i += length;
    }
    *out = chars;
    *count = n;
    return 0;
}

static int parse_translate(Parser* p, Command* cmd) {
    Buf from, to;
    size_t toCount;
    int delimiter;

    memset(&from, 0, sizeof from);
    memset(&to, 0, sizeof to);
    if (at_end(p))
        return fail(p, "unterminated `y' command");
    delimiter = p->text[p->pos++];
    if (read_delimited(p, delimiter, 0, &from) != 0 || read_delimited(p, delimiter, 0, &to) != 0)
        return fail(p, "unterminated `y' command");
    if (y_string(p, &from, &cmd->yFrom, &cmd->yCount) != 0 || y_string(p, &to, &cmd->yTo, &toCount) != 0)
        return -1;
    free(from.data);
    free(to.data);
    if (cmd->yCount != toCount)
        return fail(p, "strings for `y' command are different lengths");
    return read_end(p);
}

/* ---- commands ------------------------------------------------------------------------------ */

static int add_command(Parser* p, const Command* cmd, size_t* index) {
    Script* s = p->script;
    if (VEC_RESERVE(s->commands, p->capacity, s->count + 1) != 0)
        return fail(p, "memory exhausted");
    s->commands[s->count] = *cmd;
    *index = s->count++;
    return 0;
}

/** { opens a block that } closes; each knows the other (target). */
static int parse_block(Parser* p, Command* cmd, int addresses) {
    size_t index;

    if (cmd->name == '{') {
        if (add_command(p, cmd, &index) != 0)
            return -1;
        if (VEC_RESERVE(p->blocks, p->blockCapacity, p->blockCount + 1) != 0)
            return fail(p, "memory exhausted");
        p->blocks[p->blockCount++] = index;
        return 0;
    }
    if (addresses > 0)
        return fail(p, "} doesn't want any addresses");
    if (p->blockCount == 0)
        return fail(p, "unexpected `}'");
    if (add_command(p, cmd, &index) != 0)
        return -1;
    p->script->commands[p->blocks[--p->blockCount]].target = index;
    return read_end(p);
}

/** r R w W: the name of a file; w and W open it at once. */
static int parse_file_command(Parser* p, Command* cmd) {
    if (p->config->sandbox)
        return fail(p, "e/r/w commands disabled in sandbox mode");
    if (read_word(p, cmd, 1) != 0)
        return -1;
    if (cmd->length == 0)
        return fail(p, "missing filename in r/R/w/W commands");
    if (cmd->name == 'w' || cmd->name == 'W') {
        cmd->out = sed_output_open(p->script, (const char*)cmd->text);
        if (cmd->out == NULL)
            return fail(p, "couldn't open file %s", (const char*)cmd->text);
    }
    return 0;
}

/** q Q l: an optional number. */
static int parse_number_command(Parser* p, Command* cmd, int addresses) {
    long number;

    if ((cmd->name == 'q' || cmd->name == 'Q') && addresses > 1)
        return fail(p, "command only uses one address");
    skip_blanks(p);
    if (read_number(p, &number) == 0)
        cmd->number = number;
    return read_end(p);
}

static int parse_command(Parser* p, Command* cmd) {
    int name = p->text[p->pos++];
    int addresses = cmd->first.kind != ADDR_NONE ? (cmd->second.kind != ADDR_NONE ? 2 : 1) : 0;
    size_t index;

    cmd->name = name;
    cmd->number = -1;

    switch (name) {
        case '{': case '}':
            return parse_block(p, cmd, addresses);
        case '=': case 'd': case 'D': case 'g': case 'G': case 'h': case 'H': case 'n': case 'N':
        case 'p': case 'P': case 'x': case 'z': case 'F':
            if (read_end(p) != 0)
                return -1;
            break;
        case 'a': case 'i': case 'c':
            if (read_text(p, cmd) != 0)
                return -1;
            break;
        case ':':
            if (addresses > 0)
                return fail(p, ": doesn't want any addresses");
            if (read_word(p, cmd, 0) != 0)
                return -1;
            if (cmd->length == 0)
                return fail(p, "\":\" lacks a label");
            if (read_end(p) != 0)
                return -1;
            break;
        case 'b': case 't': case 'T':
            if (read_word(p, cmd, 0) != 0 || read_end(p) != 0)
                return -1;
            break;
        case 'r': case 'R': case 'w': case 'W':
            if (parse_file_command(p, cmd) != 0)
                return -1;
            break;
        case 'q': case 'Q': case 'l':
            if (parse_number_command(p, cmd, addresses) != 0)
                return -1;
            break;
        case 's':
            if (parse_substitute(p, cmd) != 0)
                return -1;
            break;
        case 'y':
            if (parse_translate(p, cmd) != 0)
                return -1;
            break;
        case 'v':
            if (read_word(p, cmd, 0) != 0 || read_end(p) != 0)
                return -1;
            break;
        case 'e':
            return fail(p, "the e command is not supported");
        default:
            return fail(p, "unknown command: `%c'", name);
    }
    return add_command(p, cmd, &index);
}

/** The addresses and the ! of a command; the command itself is read by parse_command. */
static int parse_addresses(Parser* p, Command* cmd) {
    if (parse_first_address(p, &cmd->first) != 0)
        return -1;
    if (cmd->first.kind != ADDR_NONE) {
        skip_blanks(p);
        if (peek(p) == ',') {
            p->pos++;
            skip_blanks(p);
            if (parse_second_address(p, &cmd->second) != 0)
                return -1;
        }
    }
    if (cmd->first.kind == ADDR_ZERO && cmd->second.kind != ADDR_REGEX)
        return fail(p, "invalid usage of line address 0");

    skip_blanks(p);
    while (peek(p) == '!') {
        cmd->negate = 1;
        p->pos++;
        skip_blanks(p);
    }
    if (at_end(p) || peek(p) == '\n' || peek(p) == ';')
        return fail(p, "missing command");
    return 0;
}

/** b, t and T name a label (or none: the end of the script): finds where it is. */
static int resolve_jumps(Parser* p) {
    Script* script = p->script;
    size_t i, j;

    for (i = 0; i < script->count; i++) {
        Command* c = &script->commands[i];
        if (c->name != 'b' && c->name != 't' && c->name != 'T')
            continue;
        if (c->length == 0) {
            c->target = script->count;
            continue;
        }
        for (j = 0; j < script->count; j++) {
            if (script->commands[j].name == ':' && strcmp((const char*)script->commands[j].text, (const char*)c->text) == 0)
                break;
        }
        if (j == script->count) {
            p->pos = p->length;
            p->panic = 1;
            return fail(p, "can't find label for jump to `%s'", (const char*)c->text);
        }
        c->target = j;
    }
    return 0;
}

static int parse_all(Parser* p) {
    Script* script = p->script;

    if (p->length >= 2 && p->text[0] == '#' && p->text[1] == 'n' && (p->length == 2 || p->text[2] == '\n'))
        script->quiet = 1;

    for (;;) {
        Command cmd;

        while (!at_end(p) && (isspace(p->text[p->pos]) || p->text[p->pos] == ';'))
            p->pos++;
        if (at_end(p))
            break;
        if (p->text[p->pos] == '#') {
            while (!at_end(p) && p->text[p->pos] != '\n')
                p->pos++;
            continue;
        }

        memset(&cmd, 0, sizeof cmd);
        if (parse_addresses(p, &cmd) != 0)
            return -1;
        if (parse_command(p, &cmd) != 0)
            return -1;
    }

    if (p->blockCount > 0)
        return fail(p, "unmatched `{'");

    return resolve_jumps(p);
}

int sed_parse(const ScriptChunk* chunks, size_t chunkCount, const SedConfig* config, Script* script,
              char* error, size_t errorSize) {
    Parser p;
    Buf all;
    size_t i;
    int status;

    memset(&p, 0, sizeof p);
    memset(&all, 0, sizeof all);
    p.chunks = chunks;
    p.chunkCount = chunkCount;
    p.chunkStart = calloc(chunkCount + 1, sizeof *p.chunkStart);
    p.config = config;
    p.script = script;
    p.error = error;
    p.errorSize = errorSize;
    if (p.chunkStart == NULL)
        return snprintf(error, errorSize, "memory exhausted"), -1;

    for (i = 0; i < chunkCount; i++) {
        if (i > 0)
            buf_add_byte(&all, '\n');
        p.chunkStart[i] = all.length;
        buf_add(&all, chunks[i].text, chunks[i].length);
    }
    buf_add_byte(&all, 0);
    p.text = all.data;
    p.length = all.length - 1;

    memset(script, 0, sizeof *script);
    status = parse_all(&p);
    free(p.blocks);
    free(p.chunkStart);
    if (status != 0) {
        free(all.data);
        return p.panic ? 4 : 1;
    }
    // the text of the commands was copied; a, i, c texts and the others own their memory
    free(all.data);
    return 0;
}
