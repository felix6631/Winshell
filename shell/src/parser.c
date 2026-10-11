#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "parser.h"

#define DELIMITERS " \t\r\n"

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
    LPSTR     text;     // only for LEX_WORD
    LPSTR     pattern;  // only for LEX_WORD: the glob pattern when the word has wildcards, else NULL
} Lexeme;

static int is_delimiter(CHAR c) {
    return strchr(DELIMITERS, c) != NULL;
}

static int is_glob_meta(CHAR c) {
    return c == '*' || c == '?' || c == '[';
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

static int push_lexeme(Lexeme** lexemes, size_t* count, size_t* capacity, LexemeType type, LPSTR text, LPSTR pattern) {
    if (*count >= *capacity) {
        Lexeme* grown = realloc(*lexemes, *capacity * 2 * sizeof **lexemes);
        if (grown == NULL)
            return -1;
        *lexemes = grown;
        *capacity *= 2;
    }
    (*lexemes)[*count].type = type;
    (*lexemes)[*count].text = text;
    (*lexemes)[*count].pattern = pattern;
    (*count)++;
    return 0;
}

/**
 * Splits line into words and operators. The words are copied into a new
 * buffer, which the word lexemes point into. Quotes are removed on the way.
 * A word is never longer than the text it comes from and every word but the
 * last is followed by a character that is not copied, so strlen(line) + 1
 * bytes are enough.
 *
 * Next to each word its glob pattern is written to a second buffer. It
 * differs only by the GLOB_ESCAPE in front of quoted wildcards, so it is at
 * most twice as long.
 */
static int tokenize(const CHAR* line, CHAR** bufferOut, CHAR** patternsOut, Lexeme** tokensOut, size_t* countOut) {
    size_t count = 0, capacity = 16;
    Lexeme* lexemes = malloc(capacity * sizeof *lexemes);
    CHAR* buffer = malloc(strlen(line) + 2);
    CHAR* patterns = malloc(strlen(line) * 2 + 2);
    const CHAR* read = line;
    CHAR* write = buffer;
    CHAR* pwrite = patterns;

    // adds a character to the word and to its pattern; literal ones are the quoted ones
#define PUT(character, literal) \
    do { \
        CHAR put_ = (character); \
        *write++ = put_; \
        if ((literal) && is_glob_meta(put_)) \
            *pwrite++ = GLOB_ESCAPE; \
        *pwrite++ = put_; \
    } while (0)

    if (lexemes == NULL || buffer == NULL || patterns == NULL)
        goto err_memory;

    while (*read != '\0') {
        CHAR quote = '\0';      // the quote character we are inside of, if any
        int quoted = 0;         // whether the word contains a quote at all
        int operatorDone = 0;
        LPSTR word, pword;

        if (is_delimiter(*read)) {
            read++;
            continue;
        }

        if (*read == '|') {
            if (push_lexeme(&lexemes, &count, &capacity, LEX_PIPE, NULL, NULL) != 0)
                goto err_memory;
            read++;
            continue;
        }
        if (*read == '<') {
            if (push_lexeme(&lexemes, &count, &capacity, LEX_INPUT, NULL, NULL) != 0)
                goto err_memory;
            read++;
            continue;
        }
        if (*read == '>') {
            LexemeType type = LEX_OUTPUT;
            if (read[1] == '>') {
                type = LEX_APPEND;
                read++;
            }
            if (push_lexeme(&lexemes, &count, &capacity, type, NULL, NULL) != 0)
                goto err_memory;
            read++;
            continue;
        }

        word = write;
        pword = pwrite;
        while (*read != '\0' && !operatorDone) {
            CHAR c = *read;

            if (quote == '\'') {
                // single quotes keep everything as it is
                if (c == '\'')
                    quote = '\0';
                else
                    PUT(c, 1);
                read++;
            } else if (c == '\\') {
                // a backslash is only special in front of a double quote:
                // 2n backslashes give n backslashes, 2n+1 give n and a literal quote
                size_t backslashes = 0, i;
                while (read[backslashes] == '\\')
                    backslashes++;

                if (read[backslashes] == '"') {
                    for (i = 0; i < backslashes / 2; i++)
                        PUT('\\', 1);
                    read += backslashes;
                    if (backslashes % 2 == 1) {
                        PUT(*read, 1);
                        read++;
                    }
                } else {
                    for (i = 0; i < backslashes; i++)
                        PUT('\\', 1);
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
            } else if (quote != '\0') {
                PUT(c, 1);
                read++;
            } else if (c == '>' && !quoted && write - word == 1 && word[0] == '2') {
                // "2>" and "2>>": the 2 is a file descriptor, not an argument
                LexemeType type = LEX_ERROR;
                if (read[1] == '>') {
                    type = LEX_ERROR_APPEND;
                    read++;
                }
                read++;
                write = word;
                pwrite = pword;
                if (push_lexeme(&lexemes, &count, &capacity, type, NULL, NULL) != 0)
                    goto err_memory;
                operatorDone = 1;
            } else if (is_delimiter(c) || c == '|' || c == '<' || c == '>') {
                break;
            } else {
                PUT(c, 0);
                read++;
            }
        }

        if (quote != '\0') {
            fprintf(stderr, "Error: Unterminated %s quote\n", quote == '"' ? "double" : "single");
            free(lexemes);
            free(buffer);
            free(patterns);
            return -1;
        }

        // a quoted word may be empty ("" is an argument), an unquoted one never is
        if (!operatorDone && (write > word || quoted)) {
            *write++ = '\0';
            *pwrite++ = '\0';
            if (push_lexeme(&lexemes, &count, &capacity, LEX_WORD, word,
                            pattern_is_active(pword) ? pword : NULL) != 0)
                goto err_memory;
        }
    }

    *bufferOut = buffer;
    *patternsOut = patterns;
    *tokensOut = lexemes;
    *countOut = count;
    return 0;

err_memory:
    fprintf(stderr, "Error: Out of memory\n");
    free(lexemes);
    free(buffer);
    free(patterns);
    return -1;
}
#undef PUT

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
