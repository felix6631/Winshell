#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "parser.h"
#include "vpath.h"

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
    LEX_ERROR_APPEND  // 2>>
} LexemeType;

typedef struct {
    LexemeType type;
    size_t     textOffset;      // into the text buffer, only for LEX_WORD
    size_t     patternOffset;   // into the pattern buffer
    int        hasPattern;      // the word has wildcards
    LPSTR      text;            // set when the buffers are final
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

    value = getenv(name);
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

typedef struct {
    Text      text, pat;
    Lexeme*   lexemes;
    size_t    count, capacity;
    int       failed;
} Lexer;

static void lexer_push(Lexer* L, LexemeType type, size_t textOffset, size_t patternOffset, int hasPattern) {
    if (L->count >= L->capacity) {
        size_t capacity = L->capacity == 0 ? 16 : L->capacity * 2;
        Lexeme* grown = realloc(L->lexemes, capacity * sizeof *grown);
        if (grown == NULL) {
            L->failed = 1;
            return;
        }
        L->lexemes = grown;
        L->capacity = capacity;
    }
    L->lexemes[L->count].type = type;
    L->lexemes[L->count].textOffset = textOffset;
    L->lexemes[L->count].patternOffset = patternOffset;
    L->lexemes[L->count].hasPattern = hasPattern;
    L->lexemes[L->count].text = NULL;
    L->lexemes[L->count].pattern = NULL;
    L->count++;
}

/** Adds a character to the word and to its pattern; literal ones are those a quote protects. */
static void lexer_char(Lexer* L, CHAR c, int literal) {
    if (text_put(&L->text, c) != 0)
        L->failed = 1;
    if (literal && is_glob_meta(c) && text_put(&L->pat, GLOB_ESCAPE) != 0)
        L->failed = 1;
    if (text_put(&L->pat, c) != 0)
        L->failed = 1;
}

/** Ends the word that began at the offsets, if it has anything in it. */
static void lexer_word(Lexer* L, size_t* wordStart, size_t* patStart, int* quoted) {
    if (L->text.length > *wordStart || *quoted) {
        text_put(&L->text, '\0');       // the terminator belongs to the word, the next one starts after it
        text_put(&L->pat, '\0');
        {
            const CHAR* pattern = L->pat.data + *patStart;
            lexer_push(L, LEX_WORD, *wordStart, *patStart, pattern_is_active(pattern));
        }
        *wordStart = L->text.length;
        *patStart = L->pat.length;
    }
    *quoted = 0;
}

/**
 * Splits line into words and operators. Quotes are removed, and the
 * variables are replaced by their values on the way. Next to each word its
 * glob pattern is written, which differs only by the GLOB_ESCAPE in front of
 * the wildcards that a quote or a variable in quotes made ordinary.
 */
static int tokenize(const CHAR* line, CHAR** bufferOut, CHAR** patternsOut, Lexeme** tokensOut, size_t* countOut) {
    Lexer L;
    const CHAR* read = line;
    size_t i;

    memset(&L, 0, sizeof L);
    text_put(&L.text, '\0');
    L.text.length = 0;
    text_put(&L.pat, '\0');
    L.pat.length = 0;

    while (*read != '\0' && !L.failed) {
        CHAR quote = '\0';      // the quote character we are inside of, if any
        int quoted = 0;         // whether the word contains a quote at all
        int operatorDone = 0;
        size_t wordStart = L.text.length, patStart = L.pat.length;

        if (is_delimiter(*read)) {
            read++;
            continue;
        }

        if (*read == '|') {
            lexer_push(&L, LEX_PIPE, 0, 0, 0);
            read++;
            continue;
        }
        if (*read == '<') {
            lexer_push(&L, LEX_INPUT, 0, 0, 0);
            read++;
            continue;
        }
        if (*read == '>') {
            LexemeType type = LEX_OUTPUT;
            if (read[1] == '>') {
                type = LEX_APPEND;
                read++;
            }
            lexer_push(&L, type, 0, 0, 0);
            read++;
            continue;
        }

        while (*read != '\0' && !operatorDone && !L.failed) {
            CHAR c = *read;

            if (quote == '\'') {
                // single quotes keep everything as it is
                if (c == '\'')
                    quote = '\0';
                else
                    lexer_char(&L, c, 1);
                read++;
            } else if (c == '\\') {
                // a backslash is only special in front of a double quote:
                // 2n backslashes give n backslashes, 2n+1 give n and a literal quote
                size_t backslashes = 0, k;
                while (read[backslashes] == '\\')
                    backslashes++;

                if (read[backslashes] == '"') {
                    for (k = 0; k < backslashes / 2; k++)
                        lexer_char(&L, '\\', 1);
                    read += backslashes;
                    if (backslashes % 2 == 1) {
                        lexer_char(&L, *read, 1);
                        read++;
                    }
                } else {
                    for (k = 0; k < backslashes; k++)
                        lexer_char(&L, '\\', 1);
                    read += backslashes;
                }
            } else if (c == '"') {
                quote = (quote == '"') ? '\0' : '"';
                quoted = 1;
                read++;
            } else if (c == '\'' && quote == '\0') {
                quote = '\'';
                quoted = 1;
                read++;
            } else if (c == '$' && (is_name_start(read[1]) || read[1] == '?' || read[1] == '{')) {
                // a variable: $NAME, ${NAME} or $?
                char name[256];
                const char* start;
                size_t length;
                const char* after;
                const char* value;
                char scratch[VPATH_MAX];
                int valid = 1;

                if (read[1] == '{') {
                    const char* close = strchr(read + 2, '}');
                    start = read + 2;
                    length = close != NULL ? (size_t)(close - start) : 0;
                    after = close != NULL ? close + 1 : read + 1;
                    valid = close != NULL && length > 0 && is_name_start(start[0]);
                    for (i = 0; valid && i < length; i++)
                        if (!is_name_char(start[i]))
                            valid = 0;
                } else if (read[1] == '?') {
                    start = read + 1;
                    length = 1;
                    after = read + 2;
                } else {
                    start = read + 1;
                    length = 0;
                    while (is_name_char(start[length]))
                        length++;
                    after = start + length;
                }

                if (!valid || length >= sizeof name) {
                    lexer_char(&L, '$', quote != '\0');     // not a variable after all
                    read++;
                } else {
                    memcpy(name, start, length);
                    name[length] = '\0';
                    value = variable_value(name, scratch, sizeof scratch);
                    read = after;
                    if (value != NULL) {
                        for (; *value != '\0'; value++) {
                            if (quote == '"') {
                                lexer_char(&L, *value, 1);          // one piece, and no wildcards
                            } else if (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r') {
                                lexer_word(&L, &wordStart, &patStart, &quoted);     // split at blanks
                            } else {
                                lexer_char(&L, *value, 0);
                            }
                        }
                    }
                }
            } else if (c == '~' && quote == '\0' && !quoted && L.text.length == wordStart &&
                       (read[1] == '\0' || read[1] == '/' || read[1] == '\\' || is_delimiter(read[1]) ||
                        read[1] == '|' || read[1] == '<' || read[1] == '>')) {
                // a lone ~ at the start of a word is the home directory
                char scratch[VPATH_MAX];
                const char* home = variable_value("HOME", scratch, sizeof scratch);
                if (home != NULL) {
                    for (; *home != '\0'; home++)
                        lexer_char(&L, *home, 1);
                    quoted = 1;             // an empty value would still be a word
                } else {
                    lexer_char(&L, '~', 0);
                }
                read++;
            } else if (quote != '\0') {
                lexer_char(&L, c, 1);
                read++;
            } else if (c == '>' && !quoted && L.text.length - wordStart == 1 && L.text.data[wordStart] == '2') {
                // "2>" and "2>>": the 2 is a file descriptor, not an argument
                LexemeType type = LEX_ERROR;
                if (read[1] == '>') {
                    type = LEX_ERROR_APPEND;
                    read++;
                }
                read++;
                L.text.length = wordStart;
                L.pat.length = patStart;
                lexer_push(&L, type, 0, 0, 0);
                operatorDone = 1;
            } else if (is_delimiter(c) || c == '|' || c == '<' || c == '>') {
                break;
            } else {
                lexer_char(&L, c, 0);
                read++;
            }
        }

        if (quote != '\0') {
            fprintf(stderr, "Error: Unterminated %s quote\n", quote == '"' ? "double" : "single");
            free(L.lexemes);
            free(L.text.data);
            free(L.pat.data);
            return -1;
        }

        // a quoted word may be empty ("" is an argument), an unquoted one never is
        if (!operatorDone)
            lexer_word(&L, &wordStart, &patStart, &quoted);
    }

    if (L.failed) {
        fprintf(stderr, "Error: Out of memory\n");
        free(L.lexemes);
        free(L.text.data);
        free(L.pat.data);
        return -1;
    }

    // the buffers do not move any more: the offsets become pointers
    for (i = 0; i < L.count; i++) {
        if (L.lexemes[i].type == LEX_WORD) {
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
        default:                 return "";
    }
}

/** Appends a new, empty command to the pipeline. */
static Command* add_command(Pipeline* pipeline, size_t* capacity) {
    Command* command;

    if (pipeline->count >= *capacity) {
        size_t newCapacity = *capacity == 0 ? 4 : *capacity * 2;
        Command* grown = realloc(pipeline->commands, newCapacity * sizeof *grown);
        if (grown == NULL)
            return NULL;
        pipeline->commands = grown;
        *capacity = newCapacity;
    }

    command = &pipeline->commands[pipeline->count++];
    memset(command, 0, sizeof *command);
    return command;
}

/** Appends a word to the arguments of a command, keeping argv NULL-terminated. */
static int add_argument(Command* command, size_t* argc, size_t* capacity, LPSTR word, LPSTR pattern) {
    if (*argc + 2 > *capacity) {
        size_t newCapacity = *capacity == 0 ? 8 : *capacity * 2;
        LPSTR* grown = realloc(command->argv, newCapacity * sizeof *grown);
        if (grown == NULL)
            return -1;
        command->argv = grown;
        grown = realloc(command->patterns, newCapacity * sizeof *grown);
        if (grown == NULL)
            return -1;
        command->patterns = grown;
        *capacity = newCapacity;
    }
    command->argv[*argc] = word;
    command->patterns[*argc] = pattern;
    (*argc)++;
    command->argv[*argc] = NULL;
    command->patterns[*argc] = NULL;
    return 0;
}

int winshell_parse(const CHAR* line, Pipeline* pipeline) {
    Lexeme* lexemes;
    size_t lexemeCount, i;
    size_t commandCapacity = 0, argc = 0, argvCapacity = 0;
    Command* current = NULL;

    memset(pipeline, 0, sizeof *pipeline);
    if (tokenize(line, &pipeline->buffer, &pipeline->patterns, &lexemes, &lexemeCount) != 0)
        return -1;

    for (i = 0; i < lexemeCount; i++) {
        Lexeme* lexeme = &lexemes[i];

        if (lexeme->type == LEX_WORD) {
            if (current == NULL) {
                current = add_command(pipeline, &commandCapacity);
                if (current == NULL)
                    goto err_memory;
                argc = argvCapacity = 0;
            }
            if (add_argument(current, &argc, &argvCapacity, lexeme->text, lexeme->pattern) != 0)
                goto err_memory;
        } else if (lexeme->type == LEX_PIPE) {
            if (current == NULL || current->argv == NULL) {
                fprintf(stderr, "Error: Syntax error: missing command before '|'\n");
                goto err_syntax;
            }
            current = NULL;
        } else {
            // a redirection: the next lexeme has to be the file name
            LPSTR* target;
            int* append = NULL;

            if (i + 1 >= lexemeCount || lexemes[i + 1].type != LEX_WORD) {
                fprintf(stderr, "Error: Syntax error: file name expected after '%s'\n", operator_name(lexeme->type));
                goto err_syntax;
            }

            if (current == NULL) {
                current = add_command(pipeline, &commandCapacity);
                if (current == NULL)
                    goto err_memory;
                argc = argvCapacity = 0;
            }

            switch (lexeme->type) {
                case LEX_INPUT:        target = &current->input;  break;
                case LEX_OUTPUT:       target = &current->output;  append = &current->append;    *append = 0; break;
                case LEX_APPEND:       target = &current->output;  append = &current->append;    *append = 1; break;
                case LEX_ERROR:        target = &current->errput;  append = &current->errAppend; *append = 0; break;
                default:                 target = &current->errput;  append = &current->errAppend; *append = 1; break;
            }
            *target = lexemes[++i].text;
        }
    }

    // a trailing '|' leaves an unfinished command behind it
    if (lexemeCount > 0 && lexemes[lexemeCount - 1].type == LEX_PIPE) {
        fprintf(stderr, "Error: Syntax error: missing command after '|'\n");
        goto err_syntax;
    }
    for (i = 0; i < pipeline->count; i++) {
        if (pipeline->commands[i].argv == NULL) {
            fprintf(stderr, "Error: Syntax error: missing command\n");
            goto err_syntax;
        }
    }

    free(lexemes);
    return 0;

err_memory:
    fprintf(stderr, "Error: Out of memory\n");
err_syntax:
    free(lexemes);
    winshell_freePipeline(pipeline);
    return -1;
}

void winshell_freePipeline(Pipeline* pipeline) {
    size_t i;
    for (i = 0; i < pipeline->count; i++) {
        free(pipeline->commands[i].argv);
        free(pipeline->commands[i].patterns);
    }
    for (i = 0; i < pipeline->ownedCount; i++)
        free(pipeline->owned[i]);
    free(pipeline->owned);
    free(pipeline->commands);
    free(pipeline->buffer);
    free(pipeline->patterns);
    memset(pipeline, 0, sizeof *pipeline);
}

/* ---- lists of pipelines ------------------------------------------------------------ */

static const char* list_operator_name(int op) {
    return op == LIST_AND ? "&&" : (op == LIST_OR ? "||" : ";");
}

static int is_blank_text(const char* start, size_t length) {
    size_t i;
    for (i = 0; i < length; i++)
        if (!is_delimiter(start[i]))
            return 0;
    return 1;
}

static int list_add(CommandList* list, const char* start, size_t length, int op) {
    ListItem* grown = realloc(list->items, (list->count + 1) * sizeof *grown);
    char* copy;

    if (grown == NULL)
        return -1;
    list->items = grown;
    copy = malloc(length + 1);
    if (copy == NULL)
        return -1;
    memcpy(copy, start, length);
    copy[length] = '\0';
    list->items[list->count].text = copy;
    list->items[list->count].op = op;
    list->count++;
    return 0;
}

int winshell_parse_list(const CHAR* line, CommandList* list) {
    size_t i = 0, segmentStart = 0;
    CHAR quote = '\0';
    int pending = LIST_FIRST;

    memset(list, 0, sizeof *list);

    for (;; i++) {
        CHAR c = line[i];
        int op = 0;
        size_t operatorLength = 0;

        if (c != '\0' && quote == '\'') {
            if (c == '\'')
                quote = '\0';
            continue;
        }

        if (c == '\\') {
            // backslashes in front of a double quote: an odd number escapes it
            size_t backslashes = 0;
            while (line[i + backslashes] == '\\')
                backslashes++;
            if (line[i + backslashes] == '"') {
                if (backslashes % 2 == 1)
                    i += backslashes;           // the quote is escaped: skip it as well
                else
                    i += backslashes - 1;       // the quote that follows toggles
            } else {
                i += backslashes - 1;
            }
            continue;
        }

        if (c == '"') {
            quote = quote == '"' ? '\0' : '"';
            continue;
        }
        if (c == '\'' && quote == '\0') {
            quote = '\'';
            continue;
        }

        // inside double quotes nothing is an operator
        if (c != '\0' && quote != '\0')
            continue;

        if (c != '\0' && quote == '\0') {
            if (c == ';') {
                op = LIST_SEQ;
                operatorLength = 1;
            } else if (c == '&' && line[i + 1] == '&') {
                op = LIST_AND;
                operatorLength = 2;
            } else if (c == '|' && line[i + 1] == '|') {
                op = LIST_OR;
                operatorLength = 2;
            } else if (c == '&') {
                fprintf(stderr, "Error: Syntax error: '&' is not supported (no background commands, no 2>&1)\n");
                goto err;
            } else if (c == '|') {
                continue;       // a pipe belongs to the segment
            } else {
                continue;
            }
        }

        if (c == '\0') {
            // the end: the last segment, which may be empty after a ';' or in an empty line
            if (is_blank_text(line + segmentStart, i - segmentStart)) {
                if (pending == LIST_AND || pending == LIST_OR) {
                    fprintf(stderr, "Error: Syntax error: missing command after '%s'\n", list_operator_name(pending));
                    goto err;
                }
                break;
            }
            if (list_add(list, line + segmentStart, i - segmentStart, pending) != 0)
                goto err_memory;
            break;
        }

        // an operator ends a segment, which has to have a command in it
        if (is_blank_text(line + segmentStart, i - segmentStart)) {
            fprintf(stderr, "Error: Syntax error near unexpected token '%s'\n", list_operator_name(op));
            goto err;
        }
        if (list_add(list, line + segmentStart, i - segmentStart, pending) != 0)
            goto err_memory;
        pending = op;
        i += operatorLength - 1;
        segmentStart = i + 1;
    }
    return 0;

err_memory:
    fprintf(stderr, "Error: Out of memory\n");
err:
    winshell_freeList(list);
    return -1;
}

void winshell_freeList(CommandList* list) {
    size_t i;
    for (i = 0; i < list->count; i++)
        free(list->items[i].text);
    free(list->items);
    memset(list, 0, sizeof *list);
}
