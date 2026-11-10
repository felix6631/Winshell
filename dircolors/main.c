/*
 * dircolors: writes the commands that set LS_COLORS from a database of colors, as GNU dircolors does. The database is
 * FILE or the one built in (-p prints it): lines "KEYWORD COLOR" for the types of files, ".ext COLOR" or "*glob COLOR" for
 * names, "TERM pattern" lines that say which terminals the lines after them are for. Without -b or -c the shell type
 * comes from $SHELL (csh or tcsh give C shell commands); where SHELL is not set the Bourne shell is taken.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "globmatch.h"
#include "textutil.h"

#define VERSION "0.1"
#define PROGRAM "dircolors"

#include "db.inc"

static void print_usage(void) {
    puts("Usage: dircolors [OPTION]... [FILE]\n"
         "Output commands to set the LS_COLORS environment variable.\n"
         "\n"
         "Determine format of output:\n"
         "  -b, --sh, --bourne-shell    output Bourne shell code to set LS_COLORS\n"
         "  -c, --csh, --c-shell        output C shell code to set LS_COLORS\n"
         "  -p, --print-database        output defaults\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "If FILE is specified, read it to determine which colors to use for which\n"
         "file types and extensions.  Otherwise, a precompiled database is used.\n"
         "For details on the format of these files, run 'dircolors --print-database'.");
}

enum { OPT_BOURNE = 1, OPT_CSH, OPT_PRINT };

static const ArgsOption options[] = {
    { OPT_BOURNE, 'b', "sh",             ARGS_NONE },
    { OPT_BOURNE, 0,   "bourne-shell",   ARGS_NONE },
    { OPT_CSH,    'c', "csh",            ARGS_NONE },
    { OPT_CSH,    0,   "c-shell",        ARGS_NONE },
    { OPT_PRINT,  'p', "print-database", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef enum { SYNTAX_UNKNOWN, SYNTAX_BOURNE, SYNTAX_CSH } Syntax;

static Syntax syntax = SYNTAX_UNKNOWN;
static int printDatabase;

static int on_option(int id, const char* value, void* context) {
    (void)value;
    (void)context;
    if (id == OPT_BOURNE)
        syntax = SYNTAX_BOURNE;
    else if (id == OPT_CSH)
        syntax = SYNTAX_CSH;
    else
        printDatabase = 1;
    return 0;
}

static const char* const slackCodes[] = {
    "NORMAL", "NORM", "FILE", "RESET", "DIR", "LNK", "LINK", "SYMLINK", "ORPHAN", "MISSING", "FIFO", "PIPE", "SOCK", "BLK",
    "BLOCK", "CHR", "CHAR", "DOOR", "EXEC", "LEFT", "LEFTCODE", "RIGHT", "RIGHTCODE", "END", "ENDCODE", "SUID", "SETUID",
    "SGID", "SETGID", "STICKY", "OTHER_WRITABLE", "OWT", "STICKY_OTHER_WRITABLE", "CAPABILITY", "MULTIHARDLINK", "CLRTOEOL", NULL
};

static const char* const lsCodes[] = {
    "no", "no", "fi", "rs", "di", "ln", "ln", "ln", "or", "mi", "pi", "pi", "so", "bd", "bd", "cd", "cd", "do", "ex", "lc", "lc",
    "rc", "rc", "ec", "ec", "su", "su", "sg", "sg", "st", "ow", "tw", "tw", "ca", "mh", "cl", NULL
};

/* ---- the output buffer ---- */

static char* out;
static size_t outLength, outCapacity;

static void put(const char* text, size_t length) {
    if (outLength + length + 1 > outCapacity) {
        outCapacity = (outLength + length + 1) * 2;
        out = realloc(out, outCapacity);
        if (out == NULL) {
            fprintf(stderr, PROGRAM ": memory exhausted\n");
            exit(1);
        }
    }
    memcpy(out + outLength, text, length);
    outLength += length;
    out[outLength] = '\0';
}

/** The text with ':' and '=' escaped, as the values of LS_COLORS need them. */
static void put_escaped(const char* text) {
    for (; *text != '\0'; text++) {
        if (*text == ':' || *text == '=')
            put("\\", 1);
        put(text, 1);
    }
}

/* ---- the database ---- */

typedef enum { ST_GLOBAL, ST_TERMNO, ST_TERMYES, ST_TERMSURE } State;

static int parse_line(char* line, const char* term, State* state, const char* fileName, unsigned long number) {
    char* keyword;
    char* arg;
    char* q;
    int unrecognized = 0, ok = 1;

    keyword = line;
    while (*keyword != '\0' && isspace((unsigned char)*keyword))
        keyword++;
    if (*keyword == '\0' || *keyword == '#')
        return 1;
    for (arg = keyword; *arg != '\0' && !isspace((unsigned char)*arg); arg++)
        ;
    if (*arg != '\0')
        *arg++ = '\0';
    while (isspace((unsigned char)*arg))
        arg++;
    // a comment starts at a '#' that begins the argument or follows a blank; trailing blanks go
    for (q = arg; *q != '\0'; q++) {
        if (*q == '#' && (q == arg || isspace((unsigned char)q[-1]))) {
            *q = '\0';
            break;
        }
    }
    q = arg + strlen(arg);
    while (q > arg && isspace((unsigned char)q[-1]))
        *--q = '\0';
    if (*arg == '\0') {
        fprintf(stderr, PROGRAM ": %s:%lu: invalid line;  missing second token\n", fileName, number);
        return 0;
    }
    if (_stricmp(keyword, "TERM") == 0) {
        if (term != NULL && glob_match(arg, term))
            *state = ST_TERMSURE;
        else if (*state != ST_TERMSURE)
            *state = ST_TERMNO;
    } else {
        if (*state == ST_TERMSURE)
            *state = ST_TERMYES;
        if (*state != ST_TERMNO) {
            if (keyword[0] == '.') {
                put("*", 1);
                put_escaped(keyword);
                put("=", 1);
                put_escaped(arg);
                put(":", 1);
            } else if (keyword[0] == '*') {
                put_escaped(keyword);
                put("=", 1);
                put_escaped(arg);
                put(":", 1);
            } else if (_stricmp(keyword, "OPTIONS") == 0 || _stricmp(keyword, "COLOR") == 0 || _stricmp(keyword, "EIGHTBIT") == 0) {
                // honored by other versions, ignored here
            } else {
                int i;

                for (i = 0; slackCodes[i] != NULL; i++) {
                    if (_stricmp(keyword, slackCodes[i]) == 0)
                        break;
                }
                if (slackCodes[i] != NULL) {
                    put(lsCodes[i], strlen(lsCodes[i]));
                    put("=", 1);
                    put_escaped(arg);
                    put(":", 1);
                } else {
                    unrecognized = 1;
                }
            }
        }
    }
    if (unrecognized) {
        fprintf(stderr, PROGRAM ": %s:%lu: unrecognized keyword %s\n", fileName, number, keyword);
        ok = 0;
    }
    return ok;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, ok = 1;
    const char* term;
    State state = ST_GLOBAL;

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (printDatabase) {
        if (syntax != SYNTAX_UNKNOWN) {
            fprintf(stderr, PROGRAM ": the options to output dircolors' internal database and\n"
                            "to select a shell syntax are mutually exclusive\n");
            args_usage_error(&spec);
            return 1;
        }
        if (count > 0) {
            fprintf(stderr, PROGRAM ": extra operand '%s'\nfile operands cannot be combined with --print-database (-p)\n", operands[0]);
            args_usage_error(&spec);
            return 1;
        }
        {
            size_t i;

            for (i = 0; defaultDatabase[i] != NULL; i++)
                printf("%s\n", defaultDatabase[i]);
        }
        return 0;
    }
    if (count > 1) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[1]);
        args_usage_error(&spec);
        return 1;
    }
    if (syntax == SYNTAX_UNKNOWN) {
        const char* shell = getenv("SHELL");

        if (shell == NULL || *shell == '\0') {
            syntax = SYNTAX_BOURNE;
        } else {
            size_t length = strlen(shell);

            if ((length >= 3 && strcmp(shell + length - 3, "csh") == 0))
                syntax = SYNTAX_CSH;
            else
                syntax = SYNTAX_BOURNE;
        }
    }
    term = getenv("TERM");
    if (count == 1) {
        FILE* in;
        unsigned char* text;
        size_t length, offset = 0;
        unsigned long number = 0;

        if (text_open(PROGRAM, operands[0], 1, &in) != 0)
            return 1;
        text = text_slurp(in, &length);
        if (in != stdin)
            fclose(in);
        if (text == NULL)
            text = (unsigned char*)calloc(1, 1);
        while (offset < length) {
            size_t start = offset, end;
            char* line;

            while (offset < length && text[offset] != '\n')
                offset++;
            end = offset;
            if (offset < length)
                offset++;
            if (end > start && text[end - 1] == '\r')
                end--;
            line = malloc(end - start + 1);
            memcpy(line, text + start, end - start);
            line[end - start] = '\0';
            number++;
            ok &= parse_line(line, term, &state, operands[0], number);
            free(line);
        }
        free(text);
    } else {
        size_t i;

        for (i = 0; defaultDatabase[i] != NULL; i++) {
            char* line = _strdup(defaultDatabase[i]);

            ok &= parse_line(line, term, &state, "<internal>", (unsigned long)i + 1);
            free(line);
        }
    }
    if (!ok)
        return 1;
    // the value, quoted for the shell
    {
        size_t i;

        if (syntax == SYNTAX_BOURNE)
            fputs("LS_COLORS='", stdout);
        else
            fputs("setenv LS_COLORS '", stdout);
        for (i = 0; i < outLength; i++) {
            if (out[i] == '\'')
                fputs("'\\''", stdout);
            else
                fputc(out[i], stdout);
        }
        if (syntax == SYNTAX_BOURNE)
            fputs("';\nexport LS_COLORS\n", stdout);
        else
            fputs("'\n", stdout);
    }
    return 0;
}
