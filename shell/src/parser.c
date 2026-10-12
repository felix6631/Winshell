#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "parser.h"
#include "vpath.h"
#include "variables.h"

#define DELIMITERS " \t\r\n"

static int lastStatus = 0;

void winshell_setLastStatus(int status) {
    lastStatus = status;
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
    if (t->length + 2 > t->capacity) {
        size_t capacity = t->capacity == 0 ? 256 : t->capacity * 2;
        CHAR* grown = realloc(t->data, capacity);
        if (grown == NULL)
            return -1;
        t->data = grown;
        t->capacity = capacity;
    }
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

    if (L->count >= L->capacity) {
        size_t capacity = L->capacity == 0 ? 16 : L->capacity * 2;
        Lexeme* grown = realloc(L->lexemes, capacity * sizeof *grown);
        if (grown == NULL) {
            L->error = LEX_NOMEM;
            return;
        }
        L->lexemes = grown;
        L->capacity = capacity;
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

/** $NAME, ${NAME} and $?: the value goes into the word, cut at blanks unless quoted or assigned. */
static int lex_variable(Lexer* L) {
    const CHAR* r = L->read;
    char name[256];
    const char* start;
    size_t length, i;
    const char* after;
    const char* value;
    char scratch[VPATH_MAX];
    int valid = 1;

    if (r[0] != '$' || !(is_name_start(r[1]) || r[1] == '?' || r[1] == '{'))
        return 0;

    if (r[1] == '{') {
        const char* close = strchr(r + 2, '}');
        start = r + 2;
        length = close != NULL ? (size_t)(close - start) : 0;
        after = close != NULL ? close + 1 : r + 1;
        valid = close != NULL && length > 0 && is_name_start(start[0]);
        for (i = 0; valid && i < length; i++)
            if (!is_name_char(start[i]))
                valid = 0;
    } else if (r[1] == '?') {
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

    if (!valid || length >= sizeof name) {
        lexer_char(L, '$', L->quote != '\0');       // not a variable after all
        L->read++;
        return 1;
    }

    memcpy(name, start, length);
    name[length] = '\0';
    value = variable_value(name, scratch, sizeof scratch);
    L->read = after;
    L->dirty = 1;
    for (; value != NULL && *value != '\0'; value++) {
        if (L->quote == '"' || L->isAssign)
            lexer_char(L, *value, 1);               // one piece, and no wildcards
        else if (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r')
            lexer_word(L);                          // split at blanks
        else
            lexer_char(L, *value, 0);
    }
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

        if (lex_single_quoted(L) || lex_backslash(L) || lex_quote_mark(L) || lex_variable(L) || lex_tilde(L) ||
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

    if (pipeline->count >= b->commandCapacity) {
        size_t capacity = b->commandCapacity == 0 ? 4 : b->commandCapacity * 2;
        Command* grown = realloc(pipeline->commands, capacity * sizeof *grown);
        if (grown == NULL)
            return NULL;
        pipeline->commands = grown;
        b->commandCapacity = capacity;
    }

    command = &pipeline->commands[pipeline->count++];
    memset(command, 0, sizeof *command);
    b->current = command;
    b->argc = b->argvCapacity = 0;
    return command;
}

/** Appends a word to the arguments of the current command, keeping argv NULL-terminated. */
static int builder_argument(Builder* b, LPSTR word, LPSTR pattern) {
    Command* command = b->current;

    if (b->argc + 2 > b->argvCapacity) {
        size_t capacity = b->argvCapacity == 0 ? 8 : b->argvCapacity * 2;
        LPSTR* grown = realloc(command->argv, capacity * sizeof *grown);
        if (grown == NULL)
            return -1;
        command->argv = grown;
        grown = realloc(command->patterns, capacity * sizeof *grown);
        if (grown == NULL)
            return -1;
        command->patterns = grown;
        b->argvCapacity = capacity;
    }
    command->argv[b->argc] = word;
    command->patterns[b->argc] = pattern;
    b->argc++;
    command->argv[b->argc] = NULL;
    command->patterns[b->argc] = NULL;
    return 0;
}

/** Whether an array that holds count items has to grow for one more (it doubles from 4). */
static int needs_room(size_t count) {
    return count == 0 || (count >= 4 && (count & (count - 1)) == 0);
}

static int add_redirect(Command* command, int fd, LPSTR file, int append, int dupOf) {
    Redirect* redirect;

    if (needs_room(command->redirectCount)) {
        size_t capacity = command->redirectCount == 0 ? 4 : command->redirectCount * 2;
        Redirect* grown = realloc(command->redirects, capacity * sizeof *grown);
        if (grown == NULL)
            return -1;
        command->redirects = grown;
    }
    redirect = &command->redirects[command->redirectCount++];
    redirect->fd = fd;
    redirect->file = file;
    redirect->append = append;
    redirect->dupOf = dupOf;
    return 0;
}

static int add_assignment(Command* command, LPSTR assignment) {
    if (needs_room(command->assignmentCount)) {
        size_t capacity = command->assignmentCount == 0 ? 4 : command->assignmentCount * 2;
        LPSTR* grown = realloc(command->assignments, capacity * sizeof *grown);
        if (grown == NULL)
            return -1;
        command->assignments = grown;
    }
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
