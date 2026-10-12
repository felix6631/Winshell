#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "nl"

static void print_usage(void) {
    puts("Usage: nl [OPTION]... [FILE]...\n"
         "Write each FILE to standard output, with line numbers added.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -b, --body-numbering=STYLE      use STYLE for numbering body lines\n"
         "  -d, --section-delimiter=CC      use CC for logical page delimiters\n"
         "  -f, --footer-numbering=STYLE    use STYLE for numbering footer lines\n"
         "  -h, --header-numbering=STYLE    use STYLE for numbering header lines\n"
         "  -i, --line-increment=NUMBER     line number increment at each line\n"
         "  -l, --join-blank-lines=NUMBER   group of NUMBER empty lines counted as one\n"
         "  -n, --number-format=FORMAT      insert line numbers according to FORMAT\n"
         "  -p, --no-renumber               do not reset line numbers for each section\n"
         "  -s, --number-separator=STRING   add STRING after (possible) line number\n"
         "  -v, --starting-line-number=NUMBER  first line number for each section\n"
         "  -w, --number-width=NUMBER       use NUMBER columns for line numbers\n"
         "      --help                      display this help and exit\n"
         "      --version                   output version information and exit\n"
         "\n"
         "Default options are: -bt -d'\\:' -fn -hn -i1 -l1 -nrn -sTAB -v1 -w6\n"
         "STYLE is one of: a (all lines), t (only non-empty lines), n (no lines).\n"
         "(The p BRE style of GNU nl is not supported.)\n"
         "FORMAT is one of: ln (left justified), rn (right justified, no leading zeros),\n"
         "rz (right justified, leading zeros).\n"
         "A CR in front of the line feed is part of the line ending, not of the line, so\n"
         "a line with only a CR LF is empty. Lines keep their endings.");
}

static char bodyStyle = 't', headerStyle = 'n', footerStyle = 'n';
static char delimiter[2] = { '\\', ':' };
static long long increment = 1, startNumber = 1, joinBlank = 1, width = 6;
static const char* format = "rn";
static const char* separator = "\t";
static int renumber = 1;

static int parse_style(const char* text, char* out, const char* what) {
    if (text[0] != '\0' && text[1] == '\0' && (text[0] == 'a' || text[0] == 't' || text[0] == 'n')) {
        *out = text[0];
        return 0;
    }
    if (text[0] == 'p')
        fprintf(stderr, PROGRAM ": the p style (numbering the lines that match a pattern) is not supported\n");
    else
        fprintf(stderr, PROGRAM ": invalid %s style: '%s'\n", what, text);
    return -1;
}

static int parse_number(const char* text, long long* out, long long minimum, const char* message) {
    char* end;
    long long value;
    if (text[0] == '\0') {
        fprintf(stderr, PROGRAM ": %s: '%s'\n", message, text);
        return -1;
    }
    value = strtoll(text, &end, 10);
    if (*end != '\0' || value < minimum) {
        fprintf(stderr, PROGRAM ": %s: '%s'\n", message, text);
        return -1;
    }
    *out = value;
    return 0;
}

static int set_option(char option, const char* value) {
    switch (option) {
        case 'b': return parse_style(value, &bodyStyle, "body numbering");
        case 'h': return parse_style(value, &headerStyle, "header numbering");
        case 'f': return parse_style(value, &footerStyle, "footer numbering");
        case 'i': return parse_number(value, &increment, 1, "invalid line number increment");
        case 'l': return parse_number(value, &joinBlank, 1, "invalid line number of blank lines");
        case 'v': return parse_number(value, &startNumber, -0x7FFFFFFFFFFFFFFFLL, "invalid starting line number");
        case 'w': return parse_number(value, &width, 1, "invalid line number field width");
        case 's': separator = value; return 0;
        case 'n':
            if (strcmp(value, "ln") != 0 && strcmp(value, "rn") != 0 && strcmp(value, "rz") != 0) {
                fprintf(stderr, PROGRAM ": invalid line numbering format: '%s'\n", value);
                return -1;
            }
            format = value;
            return 0;
        case 'd':
            if (value[0] == '\0') {
                fprintf(stderr, PROGRAM ": invalid section delimiter: ''\n");
                return -1;
            }
            delimiter[0] = value[0];
            if (value[1] != '\0')
                delimiter[1] = value[1];
            else
                delimiter[1] = ':';
            return 0;
    }
    return -1;
}

/* the id of an option is its letter */
static const ArgsOption options[] = {
    { 'b', 'b', "body-numbering",        ARGS_REQUIRED },
    { 'd', 'd', "section-delimiter",     ARGS_REQUIRED },
    { 'f', 'f', "footer-numbering",      ARGS_REQUIRED },
    { 'h', 'h', "header-numbering",      ARGS_REQUIRED },
    { 'i', 'i', "line-increment",        ARGS_REQUIRED },
    { 'l', 'l', "join-blank-lines",      ARGS_REQUIRED },
    { 'n', 'n', "number-format",         ARGS_REQUIRED },
    { 'p', 'p', "no-renumber",           ARGS_NONE },
    { 's', 's', "number-separator",      ARGS_REQUIRED },
    { 'v', 'v', "starting-line-number",  ARGS_REQUIRED },
    { 'w', 'w', "number-width",          ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    (void)context;
    if (id == 'p') {
        renumber = 0;
        return 0;
    }
    return set_option((char)id, value);
}

/** Where the numbering is: the style of the section being read, the next number and the empty lines seen. */
typedef struct {
    char      style;            // 'a', 't' or 'n'
    long long number;
    long long blankCount;
} Numbering;

/**
 * 1 for a footer delimiter, 2 for a body one and 3 for a header one: a line that is only the
 * section delimiter once, twice or three times. 0 for any other line.
 */
static int delimiter_section(const TextLine* line) {
    size_t k;

    if (line->length < 2 || line->length % 2 != 0 || line->length > 6)
        return 0;
    for (k = 0; k < line->length; k++)
        if (line->content[k] != (unsigned char)delimiter[k % 2])
            return 0;
    return (int)(line->length / 2);
}

/** Whether the line gets a number in the section being read; counts the empty lines for -l. */
static int is_numbered(Numbering* state, const TextLine* line) {
    int isBlank = line->length == 0;

    if (state->style == 'a') {
        if (isBlank && joinBlank > 1) {
            if (++state->blankCount < joinBlank)
                return 0;
        }
        state->blankCount = 0;
        return 1;
    }
    return state->style == 't' && !isBlank;
}

/** The number, in its format and width, and the separator; spaces of that width for a line without one. */
static void print_number(int numbered, long long number) {
    int columns = (int)(width > 1000000 ? 1000000 : width);

    if (!numbered) {
        printf("%*s", (int)(columns + (long long)strlen(separator)), "");
        return;
    }
    if (strcmp(format, "ln") == 0)
        printf("%-*lld", columns, number);
    else if (strcmp(format, "rz") == 0)
        printf("%0*lld", columns, number);
    else
        printf("%*lld", columns, number);
    fputs(separator, stdout);
}

/** Numbers the lines of one input. Returns 0, or 1 when it could not be read. */
static int number_lines(LineReader* reader, Numbering* state) {
    TextLine line;

    while (text_reader_next(reader, &line)) {
        int section = delimiter_section(&line);
        int numbered;

        // a delimiter starts a section and is written as an empty line
        if (section > 0) {
            state->style = section == 3 ? headerStyle : (section == 2 ? bodyStyle : footerStyle);
            if (renumber)
                state->number = startNumber;
            state->blankCount = 0;
            fputs(line.ending == 2 ? "\r\n" : "\n", stdout);
            continue;
        }

        numbered = is_numbered(state, &line);
        print_number(numbered, state->number);
        if (numbered)
            state->number += increment;

        fwrite(line.content, 1, line.length, stdout);
        // a last line without an ending gets one, as GNU nl does
        fputs(line.ending == 2 ? "\r\n" : "\n", stdout);

        // a pipe's data is passed on when the reader has nothing more of it for now
        if (reader->start >= reader->length)
            fflush(stdout);
    }
    return reader->failed ? 1 : 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    size_t operandCount, i;
    int status = 0, count, result;
    Numbering state;

    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    operandCount = (size_t)count;

    text_binary_streams();
    if (operandCount == 0)
        operands[operandCount++] = "-";

    state.style = bodyStyle;
    state.number = startNumber;
    state.blankCount = 0;

    for (i = 0; i < operandCount; i++) {
        FILE* in;
        LineReader reader;

        if (text_open(PROGRAM, operands[i], 1, &in) != 0) {
            status = 1;
            continue;
        }
        text_reader_init(&reader, in, '\n');
        if (number_lines(&reader, &state) != 0) {
            fprintf(stderr, PROGRAM ": %s: read error\n", operands[i]);
            status = 1;
        }
        text_reader_free(&reader);
        if (in != stdin)
            fclose(in);
    }

    if (fflush(stdout) != 0) {
        fprintf(stderr, PROGRAM ": write error\n");
        return 1;
    }
    return status;
}
