/*
 * csplit: splits a file into pieces at the lines that match patterns (xx00, xx01, ...) and prints the size of each
 * piece, as GNU csplit does. A pattern is a line number, /REGEXP/[OFFSET] (a piece ends before the matching line),
 * %REGEXP%[OFFSET] (skip to it) or a repeat count {N} / {*}. The input is read as a whole, so a negative offset can
 * look back without buffering. The regular expressions are POSIX basic ones.
 * It follows GNU csplit in what it does with the lines (where a scan goes on after a match, what --suppress-matched
 * drops) and in the messages; a failure removes the pieces made so far unless -k.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "regex.h"
#include "textutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "csplit"

typedef unsigned long long u64;

static void print_usage(void) {
    puts("Usage: csplit [OPTION]... FILE PATTERN...\n"
         "Output pieces of FILE separated by PATTERN(s) to files 'xx00', 'xx01', ...,\n"
         "and output byte counts of each piece to standard output.\n"
         "\n"
         "Read standard input if FILE is -\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  -b, --suffix-format=FORMAT  use sprintf FORMAT instead of %02d\n"
         "  -f, --prefix=PREFIX        use PREFIX instead of 'xx'\n"
         "  -k, --keep-files           do not remove output files on errors\n"
         "      --suppress-matched     suppress the lines matching PATTERN\n"
         "  -n, --digits=DIGITS        use specified number of digits instead of 2\n"
         "  -s, --quiet, --silent      do not print counts of output file sizes\n"
         "  -z, --elide-empty-files    remove empty output files\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Each PATTERN may be:\n"
         "  INTEGER            copy up to but not including specified line number\n"
         "  /REGEXP/[OFFSET]   copy up to but not including a matching line\n"
         "  %REGEXP%[OFFSET]   skip to, but not including a matching line\n"
         "  {INTEGER}          repeat the previous pattern specified number of times\n"
         "  {*}                repeat the previous pattern as many times as possible\n"
         "\n"
         "A line OFFSET is a required '+' or '-' followed by a positive integer.");
}

enum { OPT_SUFFIX = 1, OPT_PREFIX, OPT_KEEP, OPT_SUPPRESS, OPT_DIGITS, OPT_QUIET, OPT_ELIDE };

static const ArgsOption options[] = {
    { OPT_SUFFIX,   'b', "suffix-format",     ARGS_REQUIRED },
    { OPT_PREFIX,   'f', "prefix",            ARGS_REQUIRED },
    { OPT_KEEP,     'k', "keep-files",        ARGS_NONE },
    { OPT_SUPPRESS, 0,   "suppress-matched",  ARGS_NONE },
    { OPT_DIGITS,   'n', "digits",            ARGS_REQUIRED },
    { OPT_QUIET,    's', "quiet",             ARGS_NONE },
    { OPT_QUIET,    'q', "silent",            ARGS_NONE },
    { OPT_ELIDE,    'z', "elide-empty-files", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

/** The text as GNU quote() writes it: in single quotes, a backslash, a quote and unprintable bytes escaped. */
static const char* quote(const char* text) {
    static char buffer[2048];
    size_t n = 0;
    const unsigned char* p;

    buffer[n++] = '\'';
    for (p = (const unsigned char*)text; *p != '\0' && n < sizeof buffer - 8; p++) {
        switch (*p) {
        case '\\': buffer[n++] = '\\'; buffer[n++] = '\\'; break;
        case '\'': buffer[n++] = '\\'; buffer[n++] = '\''; break;
        case '\n': buffer[n++] = '\\'; buffer[n++] = 'n'; break;
        case '\t': buffer[n++] = '\\'; buffer[n++] = 't'; break;
        case '\r': buffer[n++] = '\\'; buffer[n++] = 'r'; break;
        default:
            if (*p < 32 || *p == 127)
                n += (size_t)snprintf(buffer + n, 8, "\\%03o", *p);
            else
                buffer[n++] = (char)*p;
        }
    }
    buffer[n++] = '\'';
    buffer[n] = '\0';
    return buffer;
}

/* ---- the output files ---- */

static int keepFiles, quiet, elideEmpty, suppressMatched;
static const char* prefix = "xx";
static char* suffixFormat;          /* the parsed -b format: pieces of text and the conversion in the middle */
static char convHead[256], convSpec[64], convTail[256];
static int useFormat;
static unsigned long digits = 2;

static char** created;              /* names of the files made, for removing them after an error */
static size_t createdCount, createdCapacity;
static FILE* outFile;
static u64 outBytes;
static u64 fileNumber;

static void cleanup_exit(int status);

static void die(const char* format, const char* argument) {
    fflush(stdout);
    fprintf(stderr, PROGRAM ": ");
    fprintf(stderr, format, argument);
    fputc('\n', stderr);
    exit(1);
}

/** The suffix of the piece with this number. */
static void make_suffix(u64 number, char* out, size_t size) {
    if (!useFormat) {
        snprintf(out, size, "%0*llu", (int)digits, number);
    } else {
        char piece[512], format[80];
        const char* p = convHead;

        out[0] = '\0';
        // head (with %% already turned into %), the number by the conversion, tail
        strncat(out, p, size - 1);
        snprintf(format, sizeof format, "%s", convSpec);
        snprintf(piece, sizeof piece, format, number);
        strncat(out, piece, size - strlen(out) - 1);
        strncat(out, convTail, size - strlen(out) - 1);
    }
}

static void create_output_file(void) {
    char suffix[1200], name[2400], native[VPATH_MAX];
    int err;

    make_suffix(fileNumber, suffix, sizeof suffix);
    snprintf(name, sizeof name, "%s%s", prefix, suffix);
    if (vpath_to_native(name, native, sizeof native) != 0) {
        fflush(stdout);
        fprintf(stderr, PROGRAM ": %s: No such file or directory\n", name);
        cleanup_exit(1);
    }
    outFile = fopen(native, "wb");
    if (outFile == NULL) {
        err = errno;
        fflush(stdout);
        fprintf(stderr, PROGRAM ": %s: %s\n", name, strerror(err));
        cleanup_exit(1);
    }
    if (createdCount == createdCapacity) {
        createdCapacity = createdCapacity == 0 ? 16 : createdCapacity * 2;
        created = realloc(created, createdCapacity * sizeof *created);
        if (created == NULL) {
            fprintf(stderr, PROGRAM ": memory exhausted\n");
            exit(1);
        }
    }
    created[createdCount++] = _strdup(native);
    fileNumber++;
    outBytes = 0;
}

static void close_output_file(void) {
    if (outFile == NULL)
        return;
    fclose(outFile);
    outFile = NULL;
    if (elideEmpty && outBytes == 0) {
        remove(created[--createdCount]);
        free(created[createdCount]);
        fileNumber--;
        return;
    }
    if (!quiet)
        printf("%llu\n", outBytes);
}

static void cleanup_exit(int status) {
    close_output_file();
    if (!keepFiles) {
        size_t i;

        for (i = 0; i < createdCount; i++)
            remove(created[i]);
    }
    exit(status);
}

/* ---- the input ---- */

static unsigned char* text;
static size_t textLength;
static size_t* lineStart;           /* of each line; a line includes its LF */
static size_t* lineLength;
static size_t lineCount;

static void split_lines(void) {
    size_t i = 0, capacity = 0;

    while (i < textLength) {
        size_t start = i;

        while (i < textLength && text[i] != '\n')
            i++;
        if (i < textLength)
            i++;
        if (lineCount == capacity) {
            capacity = capacity == 0 ? 1024 : capacity * 2;
            lineStart = realloc(lineStart, capacity * sizeof *lineStart);
            lineLength = realloc(lineLength, capacity * sizeof *lineLength);
            if (lineStart == NULL || lineLength == NULL) {
                fprintf(stderr, PROGRAM ": memory exhausted\n");
                exit(1);
            }
        }
        lineStart[lineCount] = start;
        lineLength[lineCount] = i - start;
        lineCount++;
    }
}

static void save_line(size_t index) {
    fwrite(text + lineStart[index], 1, lineLength[index], outFile);
    outBytes += lineLength[index];
}

/* ---- the patterns ---- */

typedef struct {
    int      kind;                  /* 0 line number, 1 regexp, 2 skip (regexp, output dropped) */
    u64      lines;
    Regex*   regex;
    long long offset;
    u64      repeat;
    int      forever;
    const char* arg;                /* as written, for the messages */
} Control;

static Control* controls;
static size_t controlCount;

/** An unsigned number as xstrtoumax reads it: blanks first, a + allowed, digits only after that, no overflow. */
static int parse_u64(const char* s, u64* out) {
    u64 value = 0;
    int any = 0;

    while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
        s++;
    if (*s == '+')
        s++;
    for (; *s >= '0' && *s <= '9'; s++) {
        int digit = *s - '0';

        if (value > (ULLONG_MAX - (u64)digit) / 10)
            return -1;
        value = value * 10 + (u64)digit;
        any = 1;
    }
    if (!any || *s != '\0')
        return -1;
    *out = value;
    return 0;
}

static void parse_repeat(Control* p, char* arg) {
    size_t length = strlen(arg);
    char* end = arg + length - 1;
    u64 value;

    if (*end != '}')
        die("%s: '}' is required in repeat count", quote(arg));
    *end = '\0';
    if (end - arg == 2 && arg[1] == '*') {
        p->forever = 1;
        return;
    }
    if (parse_u64(arg + 1, &value) != 0) {
        char message[2200];

        snprintf(message, sizeof message, "%s}: integer required between '{' and '}'", quote(arg));
        die("%s", message);
    }
    p->repeat = value;
}

static void parse_patterns(int count, char** operands) {
    int i;
    u64 last = 0;

    controls = calloc((size_t)count + 1, sizeof *controls);
    if (controls == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        exit(1);
    }
    for (i = 0; i < count; i++) {
        char* arg = operands[i];
        Control* p = &controls[controlCount];

        p->arg = arg;
        if (arg[0] == '/' || arg[0] == '%') {
            char delimiter = arg[0], *closing, *rest, error[256];
            const char* patterns[1];
            long long offset = 0;
            int sign = 0;

            p->kind = delimiter == '/' ? 1 : 2;
            closing = strrchr(arg, delimiter);
            if (closing == arg) {
                char message[64];

                snprintf(message, sizeof message, "closing delimiter '%c' missing", delimiter);
                fflush(stdout);
                fprintf(stderr, PROGRAM ": %s: %s\n", arg, message);
                exit(1);
            }
            rest = closing + 1;
            while (*rest != '\0' && isspace((unsigned char)*rest))
                rest++;
            if (*rest != '\0') {
                u64 magnitude = 0;

                sign = 1;
                if (*rest == '+' || *rest == '-') {
                    sign = *rest == '-' ? -1 : 1;
                    rest++;
                }
                if (*rest < '0' || *rest > '9') {
                    die("%s: integer expected after delimiter", quote(arg));
                }
                for (; *rest >= '0' && *rest <= '9'; rest++) {
                    int digit = *rest - '0';

                    if (magnitude > ((u64)LLONG_MAX - (u64)digit) / 10)
                        die("%s: integer expected after delimiter", quote(arg));
                    magnitude = magnitude * 10 + (u64)digit;
                }
                if (*rest != '\0')
                    die("%s: integer expected after delimiter", quote(arg));
                offset = sign < 0 ? -(long long)magnitude : (long long)magnitude;
            }
            p->offset = offset;
            {
                char* text2 = _strdup(arg + 1);

                text2[closing - arg - 1] = '\0';
                patterns[0] = text2;
                p->regex = regex_compile(patterns, 1, 0, error, sizeof error);
                if (p->regex == NULL) {
                    char message[2400];

                    snprintf(message, sizeof message, "%s: invalid regular expression: %s", quote(arg), error);
                    die("%s", message);
                }
            }
        } else if (arg[0] == '{') {
            die("%s: invalid pattern", quote(arg));
        } else {
            u64 value;

            if (parse_u64(arg, &value) != 0)
                die("%s: invalid pattern", quote(arg));
            if (value == 0)
                die("%s: line number must be greater than zero", arg);
            if (value < last) {
                char message[2200], number[32];

                snprintf(number, sizeof number, "%llu", last);
                snprintf(message, sizeof message, "line number %s is smaller than preceding line number, %s", quote(arg), number);
                die("%s", message);
            }
            if (value == last) {
                fflush(stdout);
                fprintf(stderr, PROGRAM ": warning: line number %s is the same as preceding line number\n", quote(arg));
            }
            last = value;
            p->kind = 0;
            p->lines = value;
        }
        controlCount++;
        if (i + 1 < count && operands[i + 1][0] == '{') {
            parse_repeat(p, operands[i + 1]);
            i++;
        }
    }
}

/* ---- the splitting ---- */

static size_t pos;                  /* the first line not yet written or dropped */
static size_t scanFrom;             /* where a regexp starts looking */
static int started;

static void handle_line_error(const Control* p, u64 repetition) {
    char number[32];

    fflush(stdout);
    if (p->kind == 0)
        snprintf(number, sizeof number, "%llu", p->lines);
    if (repetition == 0) {
        if (p->kind == 0)
            fprintf(stderr, PROGRAM ": %s: line number out of range\n", quote(number));
        else
            fprintf(stderr, PROGRAM ": %s: line number out of range\n", quote(p->arg));
    } else {
        if (p->kind == 0)
            fprintf(stderr, PROGRAM ": %s: line number out of range on repetition %llu\n", quote(number), repetition);
        else
            fprintf(stderr, PROGRAM ": %s: line number out of range on repetition %llu\n", quote(p->arg), repetition);
    }
    cleanup_exit(1);
}

static void process_line_count(const Control* p, u64 repetition) {
    u64 last;

    create_output_file();
    if (pos >= lineCount) {
        fflush(stdout);
        fprintf(stderr, PROGRAM ": input disappeared\n");
        exit(1);
    }
    last = p->lines * (repetition + 1);
    if (repetition + 1 != 0 && p->lines != 0 && last / (repetition + 1) != p->lines)
        last = ULLONG_MAX;
    while ((u64)pos + 1 < last) {
        if (pos >= lineCount)
            handle_line_error(p, repetition);
        save_line(pos++);
    }
    close_output_file();
    if (suppressMatched) {
        if (pos < lineCount)
            pos++;
    } else if (pos >= lineCount) {
        handle_line_error(p, repetition);
    }
    if (scanFrom < pos)
        scanFrom = pos;
    started = 1;
}

static int line_matches(const Control* p, size_t index) {
    static RegexInput input;
    size_t start, end, length = lineLength[index];
    const unsigned char* bytes = text + lineStart[index];

    if (length > 0 && bytes[length - 1] == '\n')
        length--;
    if (length > 0 && bytes[length - 1] == '\r')
        length--;                       // a CR LF line ends as an LF line does
    if (regex_input_set(&input, bytes, length) != 0) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        exit(1);
    }
    return regex_search(p->regex, &input, 0, &start, &end);
}

static void regexp_error(const Control* p, u64 repetition, int ignore) {
    (void)ignore;
    fflush(stdout);
    if (repetition == 0)
        fprintf(stderr, PROGRAM ": %s: match not found\n", quote(p->arg));
    else
        fprintf(stderr, PROGRAM ": %s: match not found on repetition %llu\n", quote(p->arg), repetition);
    cleanup_exit(1);
}

static void process_regexp(const Control* p, u64 repetition) {
    int ignore = p->kind == 2;
    size_t i, m;
    long long breakLine;

    if (suppressMatched && started && pos < lineCount) {
        pos++;
        if (scanFrom < pos)
            scanFrom = pos;
    }
    if (!ignore)
        create_output_file();
    for (i = scanFrom > pos ? scanFrom : pos; ; i++) {
        if (i >= lineCount) {
            if (p->forever) {
                if (!ignore) {
                    while (pos < lineCount)
                        save_line(pos++);
                    close_output_file();
                }
                exit(0);
            }
            if (!ignore) {
                while (pos < lineCount)
                    save_line(pos++);
            }
            regexp_error(p, repetition, ignore);
        }
        if (line_matches(p, i))
            break;
    }
    m = i;
    breakLine = (long long)m + p->offset;
    if (breakLine < (long long)pos || (u64)breakLine > (u64)lineCount) {
        if (breakLine > 0 && (u64)breakLine > (u64)lineCount && !ignore) {
            while (pos < lineCount)
                save_line(pos++);
        }
        handle_line_error(p, repetition);
    }
    if (!ignore) {
        while ((long long)pos < breakLine)
            save_line(pos++);
        close_output_file();
    }
    pos = (size_t)breakLine;
    scanFrom = m + (p->offset > 0 ? (size_t)p->offset : 0) + 1;
    started = 1;
}

static void run_control(const Control* p, u64 repetition) {
    if (p->kind == 0)
        process_line_count(p, repetition);
    else
        process_regexp(p, repetition);
}

static void split_file(void) {
    size_t c;

    for (c = 0; c < controlCount; c++) {
        const Control* p = &controls[c];
        u64 j;

        run_control(p, 0);
        for (j = 1; j <= p->repeat; j++)
            run_control(p, j);
        if (p->forever) {
            for (j = p->repeat + 1; ; j++)
                run_control(p, j);
        }
    }
    create_output_file();
    while (pos < lineCount)
        save_line(pos++);
    close_output_file();
}

/* ---- the options ---- */

/** Checks the format of -b: one conversion (d i u o x X with the flags ' - 0 and #, a width and a precision). */
static void check_suffix_format(const char* format) {
    const char* p = format;
    char head[256];
    size_t headLength = 0, specLength = 0;
    int alt = 0, found = 0;
    char flags[16];
    size_t flagLength = 0;

    while (*p != '\0') {
        if (*p != '%') {
            if (headLength < sizeof head - 1)
                head[headLength++] = *p;
            p++;
            continue;
        }
        if (p[1] == '%') {
            if (headLength < sizeof head - 1)
                head[headLength++] = '%';
            p += 2;
            continue;
        }
        if (found)
            break;
        // a conversion
        {
            const char* start = p;
            const char* q = p + 1;
            char width[32], precision[32];
            size_t wl = 0, pl = 0;
            int hasPrecision = 0;
            char conv;

            for (;; q++) {
                if (*q == '\'' || *q == '-' || *q == '0' || *q == '#') {
                    if (*q == '#')
                        alt = 1;
                    if (*q != '\'' && flagLength < sizeof flags - 1)
                        flags[flagLength++] = *q;
                    continue;
                }
                break;
            }
            while (*q >= '0' && *q <= '9' && wl < sizeof width - 1)
                width[wl++] = *q++;
            if (*q == '.') {
                hasPrecision = 1;
                q++;
                while (*q >= '0' && *q <= '9' && pl < sizeof precision - 1)
                    precision[pl++] = *q++;
            }
            conv = *q;
            if (conv == '\0')
                die("%s", "missing conversion specifier in suffix");
            if (strchr("diuoxX", conv) == NULL) {
                if (isprint((unsigned char)conv)) {
                    char message[64];

                    snprintf(message, sizeof message, "invalid conversion specifier in suffix: %c", conv);
                    die("%s", message);
                } else {
                    char message[64];

                    snprintf(message, sizeof message, "invalid conversion specifier in suffix: \\%.3o", (unsigned char)conv);
                    die("%s", message);
                }
            }
            if (alt && (conv == 'd' || conv == 'i' || conv == 'u')) {
                fflush(stdout);
                fprintf(stderr, PROGRAM ": invalid flags in conversion specification: %.*s\n", (int)(q - start + 1), start);
                exit(1);
            }
            // the format for snprintf with an unsigned long long
            {
                size_t n = 0;

                convSpec[n++] = '%';
                {
                    size_t k;

                    for (k = 0; k < flagLength; k++)
                        convSpec[n++] = flags[k];
                }
                memcpy(convSpec + n, width, wl);
                n += wl;
                if (hasPrecision) {
                    convSpec[n++] = '.';
                    memcpy(convSpec + n, precision, pl);
                    n += pl;
                }
                convSpec[n++] = 'l';
                convSpec[n++] = 'l';
                convSpec[n++] = (conv == 'd' || conv == 'i') ? 'u' : conv;
                convSpec[n] = '\0';
                specLength = n;
            }
            found = 1;
            p = q + 1;
            snprintf(convHead, sizeof convHead, "%.*s", (int)headLength, head);
            headLength = 0;
        }
        break;
    }
    (void)specLength;
    if (!found)
        die("%s", "missing % conversion specification in suffix");
    // what follows the conversion: text and %% only
    {
        size_t tl = 0;

        while (*p != '\0') {
            if (*p == '%') {
                if (p[1] == '%') {
                    if (tl < sizeof convTail - 1)
                        convTail[tl++] = '%';
                    p += 2;
                    continue;
                }
                die("%s", "too many % conversion specifications in suffix");
            }
            if (tl < sizeof convTail - 1)
                convTail[tl++] = *p;
            p++;
        }
        convTail[tl] = '\0';
    }
    useFormat = 1;
}

static int on_option(int id, const char* value, void* context) {
    (void)context;
    switch (id) {
    case OPT_SUFFIX:
        suffixFormat = (char*)value;
        break;
    case OPT_PREFIX:
        prefix = value;
        break;
    case OPT_KEEP:
        keepFiles = 1;
        break;
    case OPT_SUPPRESS:
        suppressMatched = 1;
        break;
    case OPT_DIGITS: {
        u64 n;
        const char* p = value;

        while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
            p++;
        if (*p == '-' && p[1] >= '0' && p[1] <= '9') {
            fprintf(stderr, PROGRAM ": invalid number: %s: Numerical result out of range\n", quote(value));
            return -1;
        }
        if (parse_u64(value, &n) != 0) {
            fprintf(stderr, PROGRAM ": invalid number: %s\n", quote(value));
            return -1;
        }
        if (n > 2000) {
            fprintf(stderr, PROGRAM ": invalid number: %s: Value too large for defined data type\n", quote(value));
            return -1;
        }
        digits = (unsigned long)n;
        break;
    }
    case OPT_QUIET:
        quiet = 1;
        break;
    case OPT_ELIDE:
        elideEmpty = 1;
        break;
    default:
        break;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result;
    FILE* in = NULL;
    char native[VPATH_MAX];

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return result == ARGS_EXIT ? 0 : 1;
    if (count < 2) {
        if (count == 0)
            fprintf(stderr, PROGRAM ": missing operand\n");
        else
            fprintf(stderr, PROGRAM ": missing operand after %s\n", quote(operands[0]));
        args_usage_error(&spec);
        return 1;
    }
    if (suffixFormat != NULL)
        check_suffix_format(suffixFormat);
    parse_patterns(count - 1, (char**)operands + 1);

    if (strcmp(operands[0], "-") != 0 && vpath_to_native(operands[0], native, sizeof native) == 0) {
        DWORD attributes;

        if (fs_attributes(native, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            // opening a directory works where GNU does it, reading it does not
            if (controls[0].kind != 2)
                create_output_file();
            fflush(stdout);
            fprintf(stderr, PROGRAM ": read error: Is a directory\n");
            cleanup_exit(1);
        }
    }
    if (text_open(PROGRAM, operands[0], 0, &in) != 0)
        return 1;
    text = text_slurp(in, &textLength);
    if (text == NULL && textLength != 0) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return 1;
    }
    if (text == NULL)
        text = (unsigned char*)calloc(1, 1);
    if (in != stdin)
        fclose(in);
    split_lines();
    split_file();
    return 0;
}
