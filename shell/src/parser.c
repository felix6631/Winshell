#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include "parser.h"
#include "vpath.h"
#include "variables.h"
#include "arith.h"
#include "vec.h"

#define DELIMITERS " \t\r\n"

static int lastStatus = 0;

void winshell_setLastStatus(int status) {
    lastStatus = status;
}

int winshell_lastStatus(void) {
    return lastStatus;
}

typedef enum {
    LEX_WORD,
    LEX_PIPE,
    LEX_INPUT,        // <
    LEX_OUTPUT,       // >
    LEX_APPEND,       // >>
    LEX_ERROR,        // 2>
    LEX_ERROR_APPEND, // 2>>
    LEX_ASSIGN,       // NAME=value in front of the command
    LEX_DUP,          // 2>&1, 1>&2: fd becomes a copy of target
    LEX_BOTH,         // &>
    LEX_BOTH_APPEND   // &>>
} LexemeType;

typedef struct {
    LexemeType type;
    size_t     textOffset;      // into the text buffer, only for LEX_WORD
    size_t     patternOffset;   // into the pattern buffer
    int        hasPattern;      // the word has wildcards
    int        fd, target;      // only for LEX_DUP
    LPSTR      text;           // set when the buffers are final
    LPSTR      pattern;
} Lexeme;

/** A string that grows. */
typedef struct {
    CHAR*  data;
    size_t length, capacity;
} Text;

static int text_put(Text* t, CHAR c) {
    if (VEC_RESERVE(t->data, t->capacity, t->length + 2) != 0)
        return -1;
    t->data[t->length++] = c;
    t->data[t->length] = '\0';
    return 0;
}

static int is_delimiter(CHAR c) {
    return c != '\0' && strchr(DELIMITERS, c) != NULL;
}

static int is_glob_meta(CHAR c) {
    return c == '*' || c == '?' || c == '[';
}

static int is_name_start(CHAR c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_name_char(CHAR c) {
    return isalnum((unsigned char)c) || c == '_';
}

/** Whether the pattern has a wildcard that is not escaped; "[" counts only with a closing "]". */
static int pattern_is_active(const CHAR* pattern) {
    const CHAR* p;
    for (p = pattern; *p != '\0'; p++) {
        if (*p == GLOB_ESCAPE) {
            if (p[1] == '\0')
                break;
            p++;
        } else if (*p == '*' || *p == '?') {
            return 1;
        } else if (*p == '[') {
            const CHAR* q;
            for (q = p + 1; *q != '\0'; q++) {
                if (*q == GLOB_ESCAPE && q[1] != '\0')
                    q++;
                else if (*q == ']')
                    return 1;
            }
        }
    }
    return 0;
}

/* ---- variables ------------------------------------------------------------------ */

/**
 * The value of a variable: $? is the status of the last command, the others
 * are the environment, which includes PWD as the shell keeps it. HOME and USER
 * are what a UNIX user expects to exist, so they come from the Windows
 * profile when the environment does not have them; HOME is a path of the
 * virtual file system. Returns NULL for a variable that is not set.
 */
static const char* variable_value(const char* name, char* scratch, size_t size) {
    const char* value;

    if (strcmp(name, "?") == 0) {
        unsigned int status = (unsigned int)lastStatus;
        // an interrupted program is 130, as on UNIX
        if (status == 0xC000013AU)
            status = 130;
        snprintf(scratch, size, "%u", status);
        return scratch;
    }

    if (strcmp(name, "$") == 0) {
        snprintf(scratch, size, "%lu", (unsigned long)GetCurrentProcessId());
        return scratch;
    }

    value = var_get(name);
    if (value != NULL)
        return value;

    if (_stricmp(name, "HOME") == 0) {
        const char* profile = getenv("USERPROFILE");
        if (profile != NULL && vpath_from_native(profile, scratch, size) == 0)
            return scratch;
    } else if (_stricmp(name, "USER") == 0) {
        return getenv("USERNAME");
    }
    return NULL;
}

/* ---- the words of one command line ---------------------------------------------- */

/*
 * The lexer turns a line into words and operators. It reads one word at a
 * time; the lex_* functions below each know one kind of character and are
 * tried in turn, a function returning non-zero when it consumed something.
 */

#define LEX_NOMEM  1
#define LEX_SYNTAX 2        /* the message has been printed */

typedef struct {
    Text      text, pat;
    Lexeme*   lexemes;
    size_t    count, capacity;
    int       error;            // 0, LEX_NOMEM or LEX_SYNTAX

    const CHAR* read;           // the next character of the line

    // the word being read
    size_t    wordStart, patStart;  // where it begins in text and in pat
    CHAR      quote;            // the quote character we are inside of, if any
    int       quoted;           // the word contains a quote, so that an empty word is still one
    int       operatorDone;     // the word was a redirection operator after all (2>)
    int       isAssign;         // the word is NAME=value
    size_t    assignValue;      // where its value starts in text
    int       dirty;            // the word has a variable or ~ in it: no NAME= or 2> can follow

    // the command being read
    int       commandWords;     // words so far, not counting assignments and file names
    int       expectFile;       // the last lexeme was a redirection that takes a file name
} Lexer;

static void lexer_push(Lexer* L, LexemeType type, size_t textOffset, size_t patternOffset, int hasPattern) {
    Lexeme* lexeme;

    if (VEC_RESERVE(L->lexemes, L->capacity, L->count + 1) != 0) {
        L->error = LEX_NOMEM;
        return;
    }
    lexeme = &L->lexemes[L->count++];
    memset(lexeme, 0, sizeof *lexeme);
    lexeme->type = type;
    lexeme->textOffset = textOffset;
    lexeme->patternOffset = patternOffset;
    lexeme->hasPattern = hasPattern;
}

static void lexer_free(Lexer* L) {
    free(L->lexemes);
    free(L->text.data);
    free(L->pat.data);
}

static void lexer_syntax_error(Lexer* L, const char* message) {
    fprintf(stderr, "Error: %s\n", message);
    L->error = LEX_SYNTAX;
}

/** Adds a character to the word and to its pattern; literal ones are those a quote protects. */
static void lexer_char(Lexer* L, CHAR c, int literal) {
    if (text_put(&L->text, c) != 0)
        L->error = LEX_NOMEM;
    if (literal && is_glob_meta(c) && text_put(&L->pat, GLOB_ESCAPE) != 0)
        L->error = LEX_NOMEM;
    if (text_put(&L->pat, c) != 0)
        L->error = LEX_NOMEM;
}

/** Ends the word that began at wordStart, if it has anything in it. */
static void lexer_word(Lexer* L) {
    if (L->text.length > L->wordStart || L->quoted) {
        text_put(&L->text, '\0');       // the terminator belongs to the word, the next one starts after it
        text_put(&L->pat, '\0');
        if (L->isAssign) {
            lexer_push(L, LEX_ASSIGN, L->wordStart, L->patStart, 0);
        } else {
            const CHAR* pattern = L->pat.data + L->patStart;
            lexer_push(L, LEX_WORD, L->wordStart, L->patStart, pattern_is_active(pattern));
            if (L->expectFile)
                L->expectFile = 0;
            else
                L->commandWords++;
        }
        L->wordStart = L->text.length;
        L->patStart = L->pat.length;
    }
    L->quoted = 0;
}

/** At an '&' after '>': 1 or 2 for "&1" or "&2" that ends a word, 0 for anything else. */
static int dup_target(const CHAR* p) {
    CHAR after;
    if (p[0] != '&' || (p[1] != '1' && p[1] != '2'))
        return 0;
    after = p[2];
    if (after != '\0' && !is_delimiter(after) && after != '|' && after != '<' && after != '>')
        return 0;
    return p[1] - '0';
}

/** Whether text[0..length) is a variable name, which an assignment starts with. */
static int is_assignment_name(const CHAR* text, size_t length) {
    size_t i;
    if (length == 0 || !is_name_start(text[0]))
        return 0;
    for (i = 1; i < length; i++)
        if (!is_name_char(text[i]))
            return 0;
    return 1;
}

/* ---- operators ------------------------------------------------------------------ */

/** read is at the '&' of ">&1": fd becomes a copy of descriptor 1 or 2. */
static void lex_dup(Lexer* L, int fd, int append) {
    int target = append ? 0 : dup_target(L->read);

    if (target == 0) {
        lexer_syntax_error(L, "Syntax error: '&' after '>' must be followed by 1 or 2");
        return;
    }
    lexer_push(L, LEX_DUP, 0, 0, 0);
    if (L->error)
        return;
    L->lexemes[L->count - 1].fd = fd;
    L->lexemes[L->count - 1].target = target;
    L->read += 2;
}

/** read is at the '>' of > or >> (or 2> and 2>>, with fd 2); a ">&n" copies a descriptor. */
static void lex_output_operator(Lexer* L, int fd) {
    int append = 0;

    L->read++;
    if (*L->read == '>') {
        append = 1;
        L->read++;
    }
    if (*L->read == '&') {
        lex_dup(L, fd, append);
        return;
    }
    if (fd == 2)
        lexer_push(L, append ? LEX_ERROR_APPEND : LEX_ERROR, 0, 0, 0);
    else
        lexer_push(L, append ? LEX_APPEND : LEX_OUTPUT, 0, 0, 0);
    L->expectFile = 1;
}

/** An operator at the start of a word: | < > >> &> &>> and the copies. Returns 1 when it was one. */
static int lex_operator(Lexer* L) {
    const CHAR* r = L->read;

    if (*r == '|') {
        lexer_push(L, LEX_PIPE, 0, 0, 0);
        L->commandWords = 0;
        L->expectFile = 0;
        L->read++;
        return 1;
    }
    if (*r == '<') {
        lexer_push(L, LEX_INPUT, 0, 0, 0);
        L->expectFile = 1;
        L->read++;
        return 1;
    }
    if (r[0] == '&' && r[1] == '>') {
        // &> and &>>: output and error to the same file
        LexemeType type = LEX_BOTH;
        L->read += 2;
        if (*L->read == '>') {
            type = LEX_BOTH_APPEND;
            L->read++;
        }
        lexer_push(L, type, 0, 0, 0);
        L->expectFile = 1;
        return 1;
    }
    if (*r == '>') {
        lex_output_operator(L, 1);
        return 1;
    }
    return 0;
}

/* ---- the characters of a word --------------------------------------------------- */

/** Inside single quotes everything is kept as it is. */
static int lex_single_quoted(Lexer* L) {
    CHAR c = *L->read;

    if (L->quote != '\'')
        return 0;
    if (c == '\'')
        L->quote = '\0';
    else
        lexer_char(L, c, 1);
    L->read++;
    return 1;
}

/**
 * A backslash is only special in front of a double quote: 2n backslashes give
 * n backslashes, 2n+1 give n and a literal quote.
 */
static int lex_backslash(Lexer* L) {
    size_t backslashes = 0, k;

    if (*L->read != '\\')
        return 0;
    while (L->read[backslashes] == '\\')
        backslashes++;

    if (L->read[backslashes] == '"') {
        for (k = 0; k < backslashes / 2; k++)
            lexer_char(L, '\\', 1);
        L->read += backslashes;
        if (backslashes % 2 == 1) {
            lexer_char(L, *L->read, 1);
            L->read++;
        }
    } else {
        for (k = 0; k < backslashes; k++)
            lexer_char(L, '\\', 1);
        L->read += backslashes;
    }
    return 1;
}

/** The characters " and ' that start or end a quote (a ' inside double quotes is only a character). */
static int lex_quote_mark(Lexer* L) {
    CHAR c = *L->read;

    if (c == '"') {
        L->quote = (L->quote == '"') ? '\0' : '"';
    } else if (c == '\'' && L->quote == '\0') {
        L->quote = '\'';
    } else {
        return 0;
    }
    L->quoted = 1;
    L->read++;
    return 1;
}

/* ---- command substitution, arithmetic and ${...} --------------------------------------------- */

static SubstitutionHandler substitutionHandler;

void winshell_setSubstitution(SubstitutionHandler handler) {
    substitutionHandler = handler;
}

static size_t piece_length(const CHAR* text);

/** Length of a double-quoted piece that starts at text[0] == '"', closing quote included; 0 when not closed. */
static size_t quoted_length(const CHAR* text) {
    size_t i = 1;

    while (text[i] != '\0' && text[i] != '"') {
        if (text[i] == '\\' && text[i + 1] == '"') {
            i += 2;
        } else if ((text[i] == '$' && (text[i + 1] == '(' || text[i + 1] == '{')) || text[i] == '`') {
            size_t n = piece_length(text + i);
            if (n == 0)
                return 0;
            i += n;
        } else {
            i++;
        }
    }
    return text[i] == '"' ? i + 1 : 0;
}

/**
 * The length of what starts at text[0] with a backtick, "${", "$((" or "$(": up to and including its end
 * (quotes and nested pieces inside are passed); 0 when it is not closed, or text is none of them.
 */
static size_t piece_length(const CHAR* text) {
    size_t i;

    if (text[0] == '`') {
        for (i = 1; text[i] != '\0'; i++) {
            if (text[i] == '\\' && text[i + 1] != '\0')
                i++;
            else if (text[i] == '`')
                return i + 1;
        }
        return 0;
    }
    if (text[0] != '$')
        return 0;

    if (text[1] == '(' && text[2] == '(') {         // $(( arithmetic ))
        int depth = 2;

        for (i = 3; text[i] != '\0'; i++) {
            if (text[i] == '(')
                depth++;
            else if (text[i] == ')' && --depth == 0)
                return i + 1;
        }
        return 0;
    }
    if (text[1] != '(' && text[1] != '{')
        return 0;

    for (i = 2; text[i] != '\0';) {
        CHAR c = text[i];

        if (c == '\\' && text[i + 1] != '\0' && (text[1] == '{' || text[i + 1] == '"')) {
            i += 2;
        } else if (c == '\'') {
            const CHAR* close = strchr(text + i + 1, '\'');
            if (close == NULL)
                return 0;
            i = (size_t)(close - text) + 1;
        } else if (c == '"') {
            size_t n = quoted_length(text + i);
            if (n == 0)
                return 0;
            i += n;
        } else if ((c == '$' && (text[i + 1] == '(' || text[i + 1] == '{')) || c == '`') {
            size_t n = piece_length(text + i);
            if (n == 0)
                return 0;
            i += n;
        } else if (c == (text[1] == '{' ? '}' : ')')) {
            return i + 1;
        } else {
            i++;
        }
    }
    return 0;
}

size_t winshell_substitution_length(const CHAR* text) {
    return piece_length(text);
}

/* ---- the value of a variable or of a substitution goes into the word ---------------------- */

/** Puts a value into the word: one piece in quotes or in an assignment, else cut at blanks into words. */
static void lexer_value(Lexer* L, const char* value) {
    L->dirty = 1;
    for (; value != NULL && *value != '\0'; value++) {
        if (L->quote == '"' || L->isAssign)
            lexer_char(L, *value, 1);               // one piece, and no wildcards
        else if (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r')
            lexer_word(L);                          // split at blanks
        else
            lexer_char(L, *value, 0);
    }
}

/**
 * The text as the words of a command are expanded (variables, substitutions, quotes), the words joined with
 * a space. Returns NULL after a message, or when out of memory (*nomem). The result has to be freed.
 */
static char* expand_text(const char* text, int* nomem) {
    Pipeline pipeline;
    char* line = malloc(strlen(text) + 4);
    char* result;
    size_t i, length = 0;

    *nomem = 0;
    if (line == NULL) {
        *nomem = 1;
        return NULL;
    }
    sprintf(line, "x %s", text);
    if (winshell_parse(line, &pipeline) != 0) {
        free(line);
        return NULL;
    }
    free(line);
    for (i = 1; pipeline.count > 0 && pipeline.commands[0].argv != NULL && pipeline.commands[0].argv[i] != NULL; i++)
        length += strlen(pipeline.commands[0].argv[i]) + 1;
    result = malloc(length + 1);
    if (result == NULL) {
        winshell_freePipeline(&pipeline);
        *nomem = 1;
        return NULL;
    }
    result[0] = '\0';
    for (i = 1; pipeline.count > 0 && pipeline.commands[0].argv != NULL && pipeline.commands[0].argv[i] != NULL; i++) {
        if (i > 1)
            strcat(result, " ");
        strcat(result, pipeline.commands[0].argv[i]);
    }
    winshell_freePipeline(&pipeline);
    return result;
}

/** $(( expression )): the variables in it are expanded first, then it is evaluated. */
static int lex_arithmetic(Lexer* L, const CHAR* r, size_t total) {
    size_t innerLength;
    char* inner;
    char* expanded;
    char message[200], number[32];
    long long value;
    int nomem;

    if (total < 5 || r[total - 2] != ')') {
        lexer_syntax_error(L, "Syntax error: bad arithmetic expression");
        return 1;
    }
    innerLength = total - 5;
    inner = malloc(innerLength + 4);
    if (inner == NULL) {
        L->error = LEX_NOMEM;
        return 1;
    }
    memcpy(inner + 1, r + 3, innerLength);
    inner[0] = '"';
    inner[innerLength + 1] = '"';
    inner[innerLength + 2] = '\0';
    L->read = r + total;

    // inside double quotes the variables and substitutions are expanded and nothing is split
    if (memchr(inner + 1, '"', innerLength) != NULL) {
        expanded = _strdup(inner + 1);
        if (expanded != NULL)
            expanded[innerLength] = '\0';
        nomem = expanded == NULL;
    } else {
        expanded = expand_text(inner, &nomem);
    }
    free(inner);
    if (expanded == NULL) {
        if (nomem)
            L->error = LEX_NOMEM;
        else
            L->error = LEX_SYNTAX;              // the message is printed
        return 1;
    }
    if (arith_eval(expanded, &value, message, sizeof message) != 0) {
        free(expanded);
        lexer_syntax_error(L, message);
        return 1;
    }
    free(expanded);
    snprintf(number, sizeof number, "%lld", value);
    lexer_value(L, number);
    return 1;
}

/** $(command), `command` and $(( expression )). */
static int lex_substitution(Lexer* L) {
    const CHAR* r = L->read;
    size_t total, skip, innerLength;
    char* inner;
    char* output = NULL;

    if (!((r[0] == '$' && r[1] == '(') || r[0] == '`'))
        return 0;

    total = piece_length(r);
    if (total == 0) {
        lexer_syntax_error(L, "Syntax error: unterminated command substitution");
        return 1;
    }
    if (r[0] == '$' && r[1] == '(' && r[2] == '(')
        return lex_arithmetic(L, r, total);
    if (substitutionHandler == NULL) {
        lexer_syntax_error(L, "Command substitution is not available here");
        return 1;
    }
    skip = r[0] == '$' ? 2 : 1;
    innerLength = total - skip - 1;
    inner = malloc(innerLength + 1);
    if (inner == NULL) {
        L->error = LEX_NOMEM;
        return 1;
    }
    memcpy(inner, r + skip, innerLength);
    inner[innerLength] = '\0';

    L->read = r + total;
    if (substitutionHandler(inner, &output) != 0 || output == NULL) {
        free(inner);
        free(output);
        L->error = LEX_NOMEM;
        return 1;
    }
    free(inner);
    {
        size_t length = strlen(output);
        while (length > 0 && (output[length - 1] == '\n' || output[length - 1] == '\r'))
            output[--length] = '\0';
    }
    lexer_value(L, output);
    free(output);
    return 1;
}

/* ---- ${NAME...} -------------------------------------------------------------------------- */

/** Whether the pattern (with * ? [..], \ and GLOB_ESCAPE escapes) matches the n characters of s completely. */
static int pattern_matches(const char* p, const char* s, size_t n) {
    for (; *p != '\0'; p++) {
        if (*p == '*') {
            size_t k;
            while (p[1] == '*')
                p++;
            for (k = 0; k <= n; k++)
                if (pattern_matches(p + 1, s + k, n - k))
                    return 1;
            return 0;
        }
        if (n == 0)
            return 0;
        if (*p == '?') {
            // any character
        } else if (*p == '[') {
            const char* q = p + 1;
            int negate = 0, matched = 0;

            if (*q == '!' || *q == '^') {
                negate = 1;
                q++;
            }
            for (; *q != '\0' && (*q != ']' || q == p + 1 + negate); q++) {
                if (q[1] == '-' && q[2] != '\0' && q[2] != ']') {
                    if (*s >= q[0] && *s <= q[2])
                        matched = 1;
                    q += 2;
                } else if (*q == *s) {
                    matched = 1;
                }
            }
            if (*q != ']')
                return 0;                       // not a set: no match
            if (matched == negate)
                return 0;
            p = q;
        } else {
            if ((*p == '\\' || *p == GLOB_ESCAPE) && p[1] != '\0')
                p++;
            if (*p != *s)
                return 0;
        }
        s++;
        n--;
    }
    return n == 0;
}

int winshell_pattern_matches(const char* pattern, const char* text) {
    return pattern_matches(pattern, text, strlen(text));
}

/** Removes the shortest or longest match of the pattern from the start or the end of value. */
static void remove_pattern(char* value, const char* pattern, int fromEnd, int longest) {
    size_t length = strlen(value), k;

    if (!fromEnd) {
        if (longest) {
            for (k = length + 1; k-- > 0;)
                if (pattern_matches(pattern, value, k)) {
                    memmove(value, value + k, length - k + 1);
                    return;
                }
        } else {
            for (k = 0; k <= length; k++)
                if (pattern_matches(pattern, value, k)) {
                    memmove(value, value + k, length - k + 1);
                    return;
                }
        }
    } else {
        if (longest) {
            for (k = 0; k <= length; k++)
                if (pattern_matches(pattern, value + k, length - k)) {
                    value[k] = '\0';
                    return;
                }
        } else {
            for (k = length + 1; k-- > 0;)
                if (pattern_matches(pattern, value + k, length - k)) {
                    value[k] = '\0';
                    return;
                }
        }
    }
}

/**
 * ${NAME}, ${#NAME}, ${NAME:-w} ${NAME-w} ${NAME:=w} ${NAME=w} ${NAME:+w} ${NAME+w} ${NAME:?w} ${NAME?w},
 * ${NAME#p} ${NAME##p} ${NAME%p} ${NAME%%p}, ${NAME:offset} and ${NAME:offset:length}. The word and the
 * pattern are expanded like the arguments of a command.
 */
static int lex_braced(Lexer* L) {
    const CHAR* r = L->read;
    size_t total = piece_length(r), bodyLength, nameLength = 0;
    char name[256];
    char scratch[VPATH_MAX];
    const char* body;
    const char* rest;
    const char* current;
    char* value = NULL;
    int set, nonnull, colon = 0, nomem;
    char op;

    if (r[0] != '$' || r[1] != '{' || total == 0)
        return 0;
    body = r + 2;
    bodyLength = total - 3;
    L->read = r + total;

    // ${#NAME}: the length of the value
    if (bodyLength > 1 && body[0] == '#') {
        size_t n;

        for (n = 1; n < bodyLength && n < sizeof name; n++) {
            if (!(is_name_char(body[n])))
                break;
            name[n - 1] = body[n];
        }
        if (n == bodyLength) {
            char number[32];
            const char* v;

            name[n - 1] = '\0';
            v = variable_value(name, scratch, sizeof scratch);
            snprintf(number, sizeof number, "%lu", (unsigned long)(v != NULL ? strlen(v) : 0));
            lexer_value(L, number);
            return 1;
        }
    }

    // the name
    if (bodyLength > 0 && is_name_start(body[0])) {
        while (nameLength < bodyLength && is_name_char(body[nameLength]))
            nameLength++;
    } else if (bodyLength > 0 && isdigit((unsigned char)body[0])) {
        while (nameLength < bodyLength && isdigit((unsigned char)body[nameLength]))
            nameLength++;
    } else if (bodyLength > 0 && strchr("?#@*!$", body[0]) != NULL) {
        nameLength = 1;
    }
    if (nameLength == 0 || nameLength >= sizeof name) {
        lexer_syntax_error(L, "Syntax error: bad substitution");
        return 1;
    }
    memcpy(name, body, nameLength);
    name[nameLength] = '\0';
    rest = body + nameLength;
    bodyLength -= nameLength;

    current = variable_value(name, scratch, sizeof scratch);
    set = current != NULL;
    nonnull = set && current[0] != '\0';
    if (bodyLength == 0) {
        lexer_value(L, current);
        return 1;
    }

    if (rest[0] == ':') {
        colon = 1;
        rest++;
        bodyLength--;
    }
    if (bodyLength == 0) {
        lexer_syntax_error(L, "Syntax error: bad substitution");
        return 1;
    }
    op = rest[0];

    if (colon && strchr("-=+?", op) == NULL) {
        // ${NAME:offset} and ${NAME:offset:length}
        char* text = malloc(bodyLength + 1);
        char* expanded;
        long offset, count = -1;
        char* colonAt;
        size_t length = current != NULL ? strlen(current) : 0;

        if (text == NULL) {
            L->error = LEX_NOMEM;
            return 1;
        }
        memcpy(text, rest, bodyLength);
        text[bodyLength] = '\0';
        colonAt = strchr(text, ':');
        if (colonAt != NULL)
            *colonAt = '\0';
        expanded = expand_text(text, &nomem);
        if (expanded == NULL) {
            free(text);
            L->error = nomem ? LEX_NOMEM : LEX_SYNTAX;
            return 1;
        }
        offset = atol(expanded);
        free(expanded);
        if (colonAt != NULL) {
            expanded = expand_text(colonAt + 1, &nomem);
            if (expanded == NULL) {
                free(text);
                L->error = nomem ? LEX_NOMEM : LEX_SYNTAX;
                return 1;
            }
            count = atol(expanded);
            free(expanded);
        }
        free(text);
        if (offset < 0)
            offset += (long)length;
        if (offset < 0 || (size_t)offset > length) {
            offset = (long)length;
        }
        if (count < 0 || (size_t)count > length - (size_t)offset)
            count = (long)(length - (size_t)offset);
        value = malloc((size_t)count + 1);
        if (value == NULL) {
            L->error = LEX_NOMEM;
            return 1;
        }
        memcpy(value, current != NULL ? current + offset : "", (size_t)count);
        value[count] = '\0';
        lexer_value(L, value);
        free(value);
        return 1;
    }

    {
        int test = colon ? nonnull : set;       // the variable counts as set
        char* word = NULL;
        size_t wordLength;
        const char* wordStart;
        int longest = 0;

        wordStart = rest + 1;
        wordLength = bodyLength - 1;
        if ((op == '#' || op == '%') && wordLength > 0 && wordStart[0] == op) {
            longest = 1;
            wordStart++;
            wordLength--;
        }
        {
            char* raw = malloc(wordLength + 1);
            if (raw == NULL) {
                L->error = LEX_NOMEM;
                return 1;
            }
            memcpy(raw, wordStart, wordLength);
            raw[wordLength] = '\0';
            word = raw;
        }

        if (op == '-' || op == '=' || op == '+' || op == '?') {
            if ((op == '-' || op == '=') && test) {
                lexer_value(L, current);
            } else if (op == '+' && !test) {
                // nothing
            } else if (op == '?' && test) {
                lexer_value(L, current);
            } else if (op == '?') {
                char* expanded = word[0] != '\0' ? expand_text(word, &nomem) : NULL;

                fprintf(stderr, "Error: %s: %s\n", name,
                        expanded != NULL ? expanded : (colon ? "parameter null or not set" : "parameter not set"));
                free(expanded);
                L->error = LEX_SYNTAX;
            } else {
                // - and = with the word, + with the word
                char* expanded = expand_text(word, &nomem);

                if (expanded == NULL) {
                    L->error = nomem ? LEX_NOMEM : LEX_SYNTAX;
                } else {
                    if (op == '=' && var_set(name, expanded) != 0)
                        L->error = LEX_NOMEM;
                    lexer_value(L, expanded);
                    free(expanded);
                }
            }
        } else if (op == '#' || op == '%') {
            char* pattern = expand_text(word, &nomem);

            if (pattern == NULL) {
                L->error = nomem ? LEX_NOMEM : LEX_SYNTAX;
            } else {
                value = _strdup(current != NULL ? current : "");
                if (value == NULL) {
                    L->error = LEX_NOMEM;
                } else {
                    remove_pattern(value, pattern, op == '%', longest);
                    lexer_value(L, value);
                    free(value);
                }
                free(pattern);
            }
        } else {
            lexer_syntax_error(L, "Syntax error: bad substitution");
        }
        free(word);
    }
    return 1;
}

/** $NAME, $? and the parameters of a script ($0 $1..$9, $#, $@, $*); ${...} goes to lex_braced. */
static int lex_variable(Lexer* L) {
    const CHAR* r = L->read;
    char name[256];
    const char* start;
    size_t length;
    const char* after;
    const char* value;
    char scratch[VPATH_MAX];

    if (r[0] == '$' && r[1] == '{') {
        if (piece_length(r) > 0)
            return lex_braced(L);
        lexer_char(L, '$', L->quote != '\0');       // not closed: an ordinary character
        L->read++;
        return 1;
    }
    if (r[0] != '$' || !(is_name_start(r[1]) || isdigit((unsigned char)r[1]) || r[1] == '?' || r[1] == '#' ||
                         r[1] == '@' || r[1] == '*' || r[1] == '!' || r[1] == '$'))
        return 0;

    if (r[1] == '?' || r[1] == '#' || r[1] == '@' || r[1] == '*' || r[1] == '!' || r[1] == '$' || isdigit((unsigned char)r[1])) {
        start = r + 1;
        length = 1;
        after = r + 2;
    } else {
        start = r + 1;
        length = 0;
        while (is_name_char(start[length]))
            length++;
        after = start + length;
    }

    if (length >= sizeof name) {
        lexer_char(L, '$', L->quote != '\0');
        L->read++;
        return 1;
    }
    memcpy(name, start, length);
    name[length] = '\0';
    value = variable_value(name, scratch, sizeof scratch);
    L->read = after;
    lexer_value(L, value);
    return 1;
}

/** A ~ alone at the start of a word (or of the value of an assignment), or in front of a separator. */
static int lex_tilde(Lexer* L)
{
    const CHAR* r = L->read;
    char scratch[VPATH_MAX];
    const char* home;

    if (r[0] != '~' || L->quote != '\0' || L->quoted)
        return 0;
    if (!(L->text.length == L->wordStart || (L->isAssign && L->text.length == L->assignValue)))
        return 0;
    if (!(r[1] == '\0' || r[1] == '/' || r[1] == '\\' || is_delimiter(r[1]) ||
          r[1] == '|' || r[1] == '<' || r[1] == '>'))
        return 0;

    home = variable_value("HOME", scratch, sizeof scratch);
    if (home != NULL) {
        for (; *home != '\0'; home++)
            lexer_char(L, *home, 1);
        L->quoted = 1;              // an empty value would still be a word
        L->dirty = 1;
    } else {
        lexer_char(L, '~', 0);
    }
    L->read++;
    return 1;
}

/** Any character inside double quotes. */
static int lex_quoted_char(Lexer* L) {
    if (L->quote == '\0')
        return 0;
    lexer_char(L, *L->read, 1);
    L->read++;
    return 1;
}

/** "2>", "2>>", "1>" and "1>>": the digit is a file descriptor, not an argument. */
static int lex_fd_redirect(Lexer* L) {
    CHAR digit;

    if (*L->read != '>' || L->quoted || L->dirty || L->text.length - L->wordStart != 1)
        return 0;
    digit = L->text.data[L->wordStart];
    if (digit != '1' && digit != '2')
        return 0;

    L->text.length = L->wordStart;      // the digit was not a word
    L->pat.length = L->patStart;
    lex_output_operator(L, digit - '0');
    L->operatorDone = 1;
    return 1;
}

/** The '=' of NAME=value in front of the command: the value is one word and has no wildcards. */
static int lex_assignment(Lexer* L) {
    if (*L->read != '=' || L->isAssign || L->quoted || L->dirty || L->commandWords != 0 || L->expectFile)
        return 0;
    if (!is_assignment_name(L->text.data + L->wordStart, L->text.length - L->wordStart))
        return 0;

    L->isAssign = 1;
    lexer_char(L, '=', 0);
    L->assignValue = L->text.length;
    L->read++;
    return 1;
}

/** Reads the characters of one word, up to a blank or an operator. */
static void lex_word_characters(Lexer* L) {
    while (*L->read != '\0' && !L->operatorDone && !L->error) {
        CHAR c = *L->read;

        if (lex_single_quoted(L) || lex_backslash(L) || lex_quote_mark(L) || lex_substitution(L) || lex_variable(L) || lex_tilde(L) ||
            lex_quoted_char(L) || lex_fd_redirect(L) || lex_assignment(L))
            continue;

        if (is_delimiter(c) || c == '|' || c == '<' || c == '>')
            break;
        lexer_char(L, c, 0);
        L->read++;
    }
}

/**
 * Splits line into words and operators. Quotes are removed, and the
 * variables are replaced by their values on the way. Next to each word its
 * glob pattern is written, which differs only by the GLOB_ESCAPE in front of
 * the wildcards that a quote or a variable in quotes made ordinary.
 */
static int tokenize(const CHAR* line, CHAR** bufferOut, CHAR** patternsOut, Lexeme** tokensOut, size_t* countOut) {
    Lexer L;
    size_t i;

    memset(&L, 0, sizeof L);
    text_put(&L.text, '\0');
    L.text.length = 0;
    text_put(&L.pat, '\0');
    L.pat.length = 0;
    L.read = line;

    while (*L.read != '\0' && !L.error) {
        if (is_delimiter(*L.read)) {
            L.read++;
            continue;
        }

        if (*L.read == '#')             // a comment: the rest of the line
            break;

        L.wordStart = L.text.length;
        L.patStart = L.pat.length;
        L.quote = '\0';
        L.quoted = 0;
        L.operatorDone = 0;
        L.isAssign = 0;
        L.dirty = 0;

        if (lex_operator(&L))
            continue;

        lex_word_characters(&L);
        if (L.error)
            break;

        if (L.quote != '\0') {
            fprintf(stderr, "Error: Unterminated %s quote\n", L.quote == '"' ? "double" : "single");
            L.error = LEX_SYNTAX;
            break;
        }

        // a quoted word may be empty ("" is an argument), an unquoted one never is
        if (!L.operatorDone)
            lexer_word(&L);
    }

    if (L.error != 0) {
        if (L.error == LEX_NOMEM)
            fprintf(stderr, "Error: Out of memory\n");
        lexer_free(&L);
        return -1;
    }

    // the buffers do not move any more: the offsets become pointers
    for (i = 0; i < L.count; i++) {
        if (L.lexemes[i].type == LEX_WORD || L.lexemes[i].type == LEX_ASSIGN) {
            L.lexemes[i].text = L.text.data + L.lexemes[i].textOffset;
            L.lexemes[i].pattern = L.lexemes[i].hasPattern ? L.pat.data + L.lexemes[i].patternOffset : NULL;
        }
    }

    *bufferOut = L.text.data;
    *patternsOut = L.pat.data;
    *tokensOut = L.lexemes;
    *countOut = L.count;
    return 0;
}

static const char* operator_name(LexemeType type) {
    switch (type) {
        case LEX_PIPE:         return "|";
        case LEX_INPUT:        return "<";
        case LEX_OUTPUT:       return ">";
        case LEX_APPEND:       return ">>";
        case LEX_ERROR:        return "2>";
        case LEX_ERROR_APPEND: return "2>>";
        case LEX_BOTH:         return "&>";
        case LEX_BOTH_APPEND:  return "&>>";
        default:                 return "";
    }
}

/* ---- commands and pipelines ------------------------------------------------------ */

/** The pipeline being built, and the command the lexemes are being added to. */
typedef struct {
    Pipeline* pipeline;
    size_t    commandCapacity;
    Command*  current;          // NULL after a '|' and at the start
    size_t    argc, argvCapacity;
} Builder;

#define PARSE_OK      0
#define PARSE_SYNTAX  (-1)     /* the message has been printed */
#define PARSE_MEMORY  (-2)

/** The command that lexemes go into; a new one is started when there is none. NULL when out of memory. */
static Command* builder_command(Builder* b) {
    Pipeline* pipeline = b->pipeline;
    Command* command;

    if (b->current != NULL)
        return b->current;

    if (VEC_RESERVE(pipeline->commands, b->commandCapacity, pipeline->count + 1) != 0)
        return NULL;

    command = &pipeline->commands[pipeline->count++];
    memset(command, 0, sizeof *command);
    b->current = command;
    b->argc = b->argvCapacity = 0;
    return command;
}

/** Appends a word to the arguments of the current command, keeping argv NULL-terminated. */
static int builder_argument(Builder* b, LPSTR word, LPSTR pattern) {
    Command* command = b->current;

    // argv and patterns grow together: the capacity is that of both
    if (b->argc + 2 > b->argvCapacity) {
        size_t patternsCapacity = b->argvCapacity;
        if (VEC_RESERVE(command->argv, b->argvCapacity, b->argc + 2) != 0 ||
            VEC_RESERVE(command->patterns, patternsCapacity, b->argc + 2) != 0)
            return -1;
    }
    command->argv[b->argc] = word;
    command->patterns[b->argc] = pattern;
    b->argc++;
    command->argv[b->argc] = NULL;
    command->patterns[b->argc] = NULL;
    return 0;
}

static int add_redirect(Command* command, int fd, LPSTR file, int append, int dupOf) {
    Redirect* redirect;

    if (VEC_GROW_BY_COUNT(command->redirects, command->redirectCount) != 0)
        return -1;
    redirect = &command->redirects[command->redirectCount++];
    redirect->fd = fd;
    redirect->file = file;
    redirect->append = append;
    redirect->dupOf = dupOf;
    return 0;
}

static int add_assignment(Command* command, LPSTR assignment) {
    if (VEC_GROW_BY_COUNT(command->assignments, command->assignmentCount) != 0)
        return -1;
    command->assignments[command->assignmentCount++] = assignment;
    return 0;
}

/** A command that has neither a program nor an assignment is not a command. */
static int has_nothing_to_run(const Command* command) {
    return command->argv == NULL && command->assignmentCount == 0;
}

/** A redirection operator at lexemes[*index]; the lexeme after it is the file, which is consumed. */
static int parse_redirection(Builder* b, const Lexeme* lexemes, size_t count, size_t* index) {
    const Lexeme* lexeme = &lexemes[*index];
    Command* command;
    LPSTR file;
    int fd = 1, append = 0, both = 0;

    if (*index + 1 >= count || lexemes[*index + 1].type != LEX_WORD) {
        fprintf(stderr, "Error: Syntax error: file name expected after '%s'\n", operator_name(lexeme->type));
        return PARSE_SYNTAX;
    }
    command = builder_command(b);
    if (command == NULL)
        return PARSE_MEMORY;
    file = lexemes[++*index].text;

    switch (lexeme->type) {
        case LEX_INPUT:
            command->input = file;
            return PARSE_OK;
        case LEX_APPEND:       append = 1; break;
        case LEX_ERROR:        fd = 2; break;
        case LEX_ERROR_APPEND: fd = 2; append = 1; break;
        case LEX_BOTH:         both = 1; break;
        case LEX_BOTH_APPEND:  both = 1; append = 1; break;
        default:               break;       // LEX_OUTPUT
    }

    // standard output and error go through the list in the order they were written
    if (add_redirect(command, fd, file, append, 0) != 0)
        return PARSE_MEMORY;
    if (both && add_redirect(command, 2, NULL, 0, 1) != 0)
        return PARSE_MEMORY;
    return PARSE_OK;
}

/** One lexeme at lexemes[*index], which a redirection may advance past its file name. */
static int parse_lexeme(Builder* b, const Lexeme* lexemes, size_t count, size_t* index) {
    const Lexeme* lexeme = &lexemes[*index];
    Command* command;

    switch (lexeme->type) {
        case LEX_WORD:
            command = builder_command(b);
            if (command == NULL || builder_argument(b, lexeme->text, lexeme->pattern) != 0)
                return PARSE_MEMORY;
            return PARSE_OK;

        case LEX_PIPE:
            if (b->current == NULL || has_nothing_to_run(b->current)) {
                fprintf(stderr, "Error: Syntax error: missing command before '|'\n");
                return PARSE_SYNTAX;
            }
            b->current = NULL;
            return PARSE_OK;

        case LEX_ASSIGN:
            command = builder_command(b);
            if (command == NULL || add_assignment(command, lexeme->text) != 0)
                return PARSE_MEMORY;
            return PARSE_OK;

        case LEX_DUP:
            command = builder_command(b);
            if (command == NULL || add_redirect(command, lexeme->fd, NULL, 0, lexeme->target) != 0)
                return PARSE_MEMORY;
            return PARSE_OK;

        default:
            return parse_redirection(b, lexemes, count, index);
    }
}

int winshell_parse(const CHAR* line, Pipeline* pipeline) {
    Lexeme* lexemes;
    size_t lexemeCount, i;
    Builder builder;
    int status = PARSE_OK;

    memset(pipeline, 0, sizeof *pipeline);
    if (tokenize(line, &pipeline->buffer, &pipeline->patterns, &lexemes, &lexemeCount) != 0)
        return -1;

    memset(&builder, 0, sizeof builder);
    builder.pipeline = pipeline;

    for (i = 0; i < lexemeCount && status == PARSE_OK; i++)
        status = parse_lexeme(&builder, lexemes, lexemeCount, &i);

    if (status == PARSE_OK && lexemeCount > 0 && lexemes[lexemeCount - 1].type == LEX_PIPE) {
        // a trailing '|' leaves an unfinished command behind it
        fprintf(stderr, "Error: Syntax error: missing command after '|'\n");
        status = PARSE_SYNTAX;
    }
    for (i = 0; status == PARSE_OK && i < pipeline->count; i++) {
        if (has_nothing_to_run(&pipeline->commands[i])) {
            fprintf(stderr, "Error: Syntax error: missing command\n");
            status = PARSE_SYNTAX;
        }
    }

    free(lexemes);
    if (status == PARSE_MEMORY)
        fprintf(stderr, "Error: Out of memory\n");
    if (status != PARSE_OK) {
        winshell_freePipeline(pipeline);
        return -1;
    }
    return 0;
}

void winshell_freePipeline(Pipeline* pipeline) {
    size_t i;
    for (i = 0; i < pipeline->count; i++) {
        free(pipeline->commands[i].argv);
        free(pipeline->commands[i].patterns);
        free(pipeline->commands[i].redirects);
        free(pipeline->commands[i].assignments);
    }
    for (i = 0; i < pipeline->ownedCount; i++)
        free(pipeline->owned[i]);
    free(pipeline->owned);
    free(pipeline->commands);
    free(pipeline->buffer);
    free(pipeline->patterns);
    memset(pipeline, 0, sizeof *pipeline);
}
