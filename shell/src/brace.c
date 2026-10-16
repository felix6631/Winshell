#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "brace.h"
#include "parser.h"

#define BRACE_LIMIT 65536       /* words from one word of the line */

typedef struct {
    char** items;
    size_t count, capacity;
} Words;

static void words_free(Words* w) {
    size_t i;

    for (i = 0; i < w->count; i++)
        free(w->items[i]);
    free(w->items);
    memset(w, 0, sizeof *w);
}

/** Takes the string (malloc'ed). Returns 0, or -1 when out of memory or over the limit. */
static int words_add(Words* w, char* text) {
    if (text == NULL || w->count >= BRACE_LIMIT) {
        free(text);
        return -1;
    }
    if (w->count == w->capacity) {
        size_t capacity = w->capacity == 0 ? 8 : w->capacity * 2;
        char** grown = realloc(w->items, capacity * sizeof *grown);

        if (grown == NULL) {
            free(text);
            return -1;
        }
        w->items = grown;
        w->capacity = capacity;
    }
    w->items[w->count++] = text;
    return 0;
}

static char* join3(const char* a, size_t aLength, const char* b, size_t bLength, const char* c) {
    size_t cLength = strlen(c);
    char* result = malloc(aLength + bLength + cLength + 1);

    if (result == NULL)
        return NULL;
    memcpy(result, a, aLength);
    memcpy(result + aLength, b, bLength);
    memcpy(result + aLength + bLength, c, cLength + 1);
    return result;
}

/** If s[i] starts a quote, a $(...), a ${...} or a backtick: the index after it; else i itself. */
static size_t skip_hidden(const char* s, size_t i) {
    char c = s[i];

    if (c == '\'') {
        const char* close = strchr(s + i + 1, '\'');

        return close != NULL ? (size_t)(close - s) + 1 : strlen(s);
    }
    if (c == '"') {
        size_t j = i + 1;

        while (s[j] != '\0' && s[j] != '"') {
            if ((s[j] == '$' && (s[j + 1] == '(' || s[j + 1] == '{')) || s[j] == '`') {
                size_t n = winshell_substitution_length(s + j);

                j += n > 0 ? n : 1;
            } else if (s[j] == '\\' && s[j + 1] == '"') {
                j += 2;
            } else {
                j++;
            }
        }
        return s[j] != '\0' ? j + 1 : j;
    }
    if ((c == '$' && (s[i + 1] == '(' || s[i + 1] == '{')) || c == '`') {
        size_t n = winshell_substitution_length(s + i);

        return n > 0 ? i + n : i + 1;
    }
    return i;
}

static int is_integer_text(const char* s, size_t n) {
    size_t i = 0;

    if (n > 0 && s[0] == '-')
        i = 1;
    if (i >= n)
        return 0;
    for (; i < n; i++)
        if (!isdigit((unsigned char)s[i]))
            return 0;
    return 1;
}

static int has_leading_zero(const char* s, size_t n) {
    if (n > 0 && s[0] == '-') {
        s++;
        n--;
    }
    return n > 1 && s[0] == '0';
}

/**
 * The items of a sequence x..y[..step] (inner is the text between the braces). Returns 1 when it is one (the
 * items are added), 0 when it is not, -1 on a failure.
 */
static int sequence_items(const char* inner, size_t length, Words* items) {
    const char* firstDots = NULL;
    const char* secondDots = NULL;
    size_t i;
    const char* partA;
    const char* partB;
    const char* partC = NULL;
    size_t lengthA, lengthB, lengthC = 0;
    long step = 1;

    for (i = 0; i + 1 < length; i++) {
        if (inner[i] == '.' && inner[i + 1] == '.') {
            if (firstDots == NULL)
                firstDots = inner + i;
            else if (secondDots == NULL)
                secondDots = inner + i;
            else
                return 0;
            i++;
        }
    }
    if (firstDots == NULL)
        return 0;
    partA = inner;
    lengthA = (size_t)(firstDots - inner);
    partB = firstDots + 2;
    lengthB = secondDots != NULL ? (size_t)(secondDots - partB) : length - lengthA - 2;
    if (secondDots != NULL) {
        partC = secondDots + 2;
        lengthC = length - (size_t)(partC - inner);
        if (!is_integer_text(partC, lengthC))
            return 0;
        step = strtol(partC, NULL, 10);
        if (step < 0)
            step = -step;
        if (step == 0)
            step = 1;
    }

    if (is_integer_text(partA, lengthA) && is_integer_text(partB, lengthB)) {
        long a = strtol(partA, NULL, 10), b = strtol(partB, NULL, 10), value;
        int width = 0, direction = a <= b ? 1 : -1;
        char text[64];

        if (has_leading_zero(partA, lengthA) || has_leading_zero(partB, lengthB))
            width = (int)(lengthA > lengthB ? lengthA : lengthB);
        if (lengthA > 15 || lengthB > 15)
            return 0;
        if ((b > a ? b - a : a - b) / step >= BRACE_LIMIT)
            return -1;
        for (value = a; direction > 0 ? value <= b : value >= b; value += direction * step) {
            snprintf(text, sizeof text, "%0*ld", width, value);
            if (words_add(items, _strdup(text)) != 0)
                return -1;
        }
        return 1;
    }
    if (lengthA == 1 && lengthB == 1 && isalpha((unsigned char)partA[0]) && isalpha((unsigned char)partB[0])) {
        int a = (unsigned char)partA[0], b = (unsigned char)partB[0], value, direction = a <= b ? 1 : -1;

        for (value = a; direction > 0 ? value <= b : value >= b; value += direction * (int)step) {
            char text[2] = { (char)value, '\0' };

            if (words_add(items, _strdup(text)) != 0)
                return -1;
        }
        return 1;
    }
    return 0;
}

static int expand_from(const char* text, size_t from, Words* out);

/**
 * Looks for the first brace expression at or after `from`. Returns 1 when there is one: its `{` at *open,
 * its `}` at *close, and the items in *items (comma-separated parts as they are, or a sequence); 0 when
 * there is none; -1 on a failure.
 */
static int find_brace(const char* text, size_t from, size_t* open, size_t* close, Words* items) {
    size_t i = from;

    while (text[i] != '\0') {
        size_t hidden = skip_hidden(text, i);
        size_t j, partStart;
        int depth;
        int commas = 0;
        Words parts;

        if (hidden != i) {
            i = hidden;
            continue;
        }
        if (text[i] == '\\' && (text[i + 1] == '{' || text[i + 1] == '}' || text[i + 1] == ',')) {
            i += 2;                         // an escaped brace is an ordinary character
            continue;
        }
        if (text[i] != '{') {
            i++;
            continue;
        }

        // the matching }, the commas at the top level on the way
        memset(&parts, 0, sizeof parts);
        depth = 1;
        partStart = i + 1;
        for (j = i + 1; text[j] != '\0' && depth > 0;) {
            size_t skipped = skip_hidden(text, j);

            if (skipped != j) {
                j = skipped;
                continue;
            }
            if (text[j] == '{') {
                depth++;
            } else if (text[j] == '}') {
                depth--;
                if (depth == 0)
                    break;
            } else if (text[j] == ',' && depth == 1) {
                char* part = malloc(j - partStart + 1);

                if (part == NULL) {
                    words_free(&parts);
                    return -1;
                }
                memcpy(part, text + partStart, j - partStart);
                part[j - partStart] = '\0';
                if (words_add(&parts, part) != 0) {
                    words_free(&parts);
                    return -1;
                }
                commas++;
                partStart = j + 1;
            }
            j++;
        }
        if (text[j] != '}' || depth != 0) {
            words_free(&parts);
            i++;                            // never closed: an ordinary character
            continue;
        }

        if (commas > 0) {
            char* last = malloc(j - partStart + 1);

            if (last == NULL) {
                words_free(&parts);
                return -1;
            }
            memcpy(last, text + partStart, j - partStart);
            last[j - partStart] = '\0';
            if (words_add(&parts, last) != 0) {
                words_free(&parts);
                return -1;
            }
            *items = parts;
            *open = i;
            *close = j;
            return 1;
        }
        words_free(&parts);
        {
            int sequence = sequence_items(text + i + 1, j - i - 1, items);

            if (sequence < 0) {
                words_free(items);
                return -1;
            }
            if (sequence > 0) {
                *open = i;
                *close = j;
                return 1;
            }
        }
        i++;                                // {a} or {}: ordinary; a brace inside may still be an expression
    }
    return 0;
}

static int expand_from(const char* text, size_t from, Words* out) {
    size_t open = 0, close = 0, k;
    Words items;
    int found;

    memset(&items, 0, sizeof items);
    found = find_brace(text, from, &open, &close, &items);
    if (found < 0)
        return -1;
    if (found == 0)
        return words_add(out, _strdup(text));

    for (k = 0; k < items.count; k++) {
        char* combined = join3(text, open, items.items[k], strlen(items.items[k]), text + close + 1);

        if (combined == NULL || expand_from(combined, open, out) != 0) {
            free(combined);
            words_free(&items);
            return -1;
        }
        free(combined);
    }
    words_free(&items);
    return 0;
}

static int is_word_end(char c) {
    return c == '\0' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || strchr("|<>;&()", c) != NULL;
}

/** The end of the word that starts at s[i]: quotes and substitutions are passed. */
static size_t word_end(const char* s, size_t i) {
    while (!is_word_end(s[i])) {
        size_t hidden = skip_hidden(s, i);

        i = hidden != i ? hidden : i + 1;
    }
    return i;
}

static int is_assignment_word(const char* s, size_t length) {
    size_t i;

    if (length == 0 || !(isalpha((unsigned char)s[0]) || s[0] == '_'))
        return 0;
    for (i = 1; i < length && (isalnum((unsigned char)s[i]) || s[i] == '_'); i++)
        ;
    return i < length && s[i] == '=';
}

static int enabled = 1;

int winshell_brace_enable(int on) {
    int before = enabled;

    enabled = on;
    return before;
}

int winshell_brace_expand(const char* line, char** result) {
    size_t i = 0, used = 0, capacity;
    char* out;
    int changed = 0;
    char previous = '\0';           // the last character that was not a blank, in front of the word

    *result = NULL;
    if (!enabled || strchr(line, '{') == NULL)
        return 0;
    capacity = strlen(line) * 2 + 64;
    out = malloc(capacity);
    if (out == NULL)
        return -1;

    while (line[i] != '\0') {
        size_t end, length;
        char c = line[i];

        if (is_word_end(c)) {
            if (used + 2 > capacity) {
                char* grown = realloc(out, capacity *= 2);

                if (grown == NULL) {
                    free(out);
                    return -1;
                }
                out = grown;
            }
            out[used++] = c;
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                previous = c;
            i++;
            continue;
        }

        end = word_end(line, i);
        length = end - i;
        {
            char* word = malloc(length + 1);
            Words words;
            int expand = 0;

            if (word == NULL) {
                free(out);
                return -1;
            }
            memcpy(word, line + i, length);
            word[length] = '\0';
            memset(&words, 0, sizeof words);
            if (previous != '<' && previous != '>' && !is_assignment_word(word, length) && strchr(word, '{') != NULL) {
                int status = expand_from(word, 0, &words);

                if (status < 0) {
                    words_free(&words);     // too many words, or no memory: the word stays as it is
                } else if (words.count > 1 || (words.count == 1 && strcmp(words.items[0], word) != 0)) {
                    expand = 1;
                }
            }
            if (!expand) {
                words_free(&words);
                words.items = malloc(sizeof *words.items);
                if (words.items == NULL) {
                    free(word);
                    free(out);
                    return -1;
                }
                words.items[0] = word;
                words.count = 1;
                word = NULL;
            } else {
                free(word);
                changed = 1;
            }
            {
                size_t k, need = 1;

                for (k = 0; k < words.count; k++)
                    need += strlen(words.items[k]) + 1;
                if (used + need + 1 > capacity) {
                    char* grown;

                    capacity = (used + need) * 2;
                    grown = realloc(out, capacity);
                    if (grown == NULL) {
                        words_free(&words);
                        free(out);
                        return -1;
                    }
                    out = grown;
                }
                for (k = 0; k < words.count; k++) {
                    size_t n = strlen(words.items[k]);

                    if (k > 0)
                        out[used++] = ' ';
                    memcpy(out + used, words.items[k], n);
                    used += n;
                }
            }
            words_free(&words);
        }
        previous = 'w';
        i = end;
    }
    out[used] = '\0';
    if (!changed) {
        free(out);
        return 0;
    }
    *result = out;
    return 1;
}
