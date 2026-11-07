/*
 * split: cuts a file into pieces named PREFIXaa, PREFIXab, ... (or with numbers: -d, -x), by lines (-l), bytes
 * (-b), lines of at most so many bytes (-C), or into N pieces (-n N, K/N, l/N, l/K/N, r/N, r/K/N), as GNU split
 * does. With numeric suffixes and no -a the suffix grows when it runs out (x89 is followed by x9000).
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "textutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "split"

static void print_usage(void) {
    puts("Usage: split [OPTION]... [FILE [PREFIX]]\n"
         "Output pieces of FILE to PREFIXaa, PREFIXab, ...;\n"
         "default size is 1000 lines, and default PREFIX is 'x'.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -a, --suffix-length=N   generate suffixes of length N (default 2)\n"
         "      --additional-suffix=SUFFIX  append an additional SUFFIX to file names\n"
         "  -b, --bytes=SIZE        put SIZE bytes per output file\n"
         "  -C, --line-bytes=SIZE   put at most SIZE bytes of records per output file\n"
         "  -d                      use numeric suffixes starting at 0, not alphabetic\n"
         "      --numeric-suffixes[=FROM]  same as -d, but allow setting the start value\n"
         "  -x                      use hex suffixes starting at 0, not alphabetic\n"
         "      --hex-suffixes[=FROM]  same as -x, but allow setting the start value\n"
         "  -e, --elide-empty-files  do not generate empty output files with '-n'\n"
         "  -l, --lines=NUMBER      put NUMBER lines/records per output file\n"
         "  -n, --number=CHUNKS     generate CHUNKS output files; see explanation below\n"
         "  -t, --separator=SEP     use SEP instead of newline as the record separator\n"
         "  -u, --unbuffered        accepted\n"
         "      --verbose           print a diagnostic just before each output file is opened\n"
         "      --help              display this help and exit\n"
         "      --version           output version information and exit\n"
         "\n"
         "CHUNKS may be:\n"
         "  N       split into N files based on size of input\n"
         "  K/N     output Kth of N to stdout\n"
         "  l/N     split into N files without splitting lines/records\n"
         "  l/K/N   output Kth of N to stdout without splitting lines/records\n"
         "  r/N     like 'l' but use round robin distribution\n"
         "  r/K/N   likewise but only output Kth of N to stdout\n"
         "\n"
         "SIZE is an integer and optional unit (example: 10M is 10*1024*1024).  Units\n"
         "are K, M, G, T, P, E, Z, Y (powers of 1024) or KB, MB, ... (powers of 1000).");
}

enum { OPT_SUFFIX_LENGTH = 1, OPT_ADDITIONAL, OPT_BYTES, OPT_LINE_BYTES, OPT_NUMERIC_SHORT, OPT_NUMERIC, OPT_HEX_SHORT, OPT_HEX,
       OPT_ELIDE, OPT_LINES, OPT_NUMBER, OPT_SEPARATOR, OPT_UNBUFFERED, OPT_VERBOSE, OPT_FILTER };

static const ArgsOption options[] = {
    { OPT_SUFFIX_LENGTH, 'a', "suffix-length",       ARGS_REQUIRED },
    { OPT_ADDITIONAL,    0,   "additional-suffix",   ARGS_REQUIRED },
    { OPT_BYTES,         'b', "bytes",               ARGS_REQUIRED },
    { OPT_LINE_BYTES,    'C', "line-bytes",          ARGS_REQUIRED },
    { OPT_NUMERIC_SHORT, 'd', NULL,                  ARGS_NONE },
    { OPT_NUMERIC,       0,   "numeric-suffixes",    ARGS_OPTIONAL },
    { OPT_HEX_SHORT,     'x', NULL,                  ARGS_NONE },
    { OPT_HEX,           0,   "hex-suffixes",        ARGS_OPTIONAL },
    { OPT_ELIDE,         'e', "elide-empty-files",   ARGS_NONE },
    { OPT_LINES,         'l', "lines",               ARGS_REQUIRED },
    { OPT_NUMBER,        'n', "number",              ARGS_REQUIRED },
    { OPT_SEPARATOR,     't', "separator",           ARGS_REQUIRED },
    { OPT_UNBUFFERED,    'u', "unbuffered",          ARGS_NONE },
    { OPT_VERBOSE,       0,   "verbose",             ARGS_NONE },
    { OPT_FILTER,        0,   "filter",              ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 1 };

typedef enum { BY_LINES, BY_BYTES, BY_LINE_BYTES, BY_CHUNKS } Mode;

typedef struct {
    Mode      mode;
    long long size;             /* lines, bytes or line bytes */
    long long chunks, chunk;    /* -n: N and K (0 for all of them) */
    char      chunkKind;        /* 'b' bytes, 'l' lines, 'r' round robin */
    int       suffixLength;
    int       suffixGiven;
    char      style;            /* 'a', 'd' or 'x' */
    long long from;
    const char* additional;
    int       elide, verbose;
    int       separator;
    const char* separatorText;
    int       conflict;         /* two ways of splitting were asked for */
    int       typeGiven;
} Settings;

/** Reads a size: digits and a unit (K M G T P E Z Y, KB MB ..., b = 512). Returns 0, or -1. */
static int parse_size(const char* text, long long* out) {
    char* end;
    long long value, multiplier = 1;
    static const char units[] = "KMGTPEZY";

    if (!isdigit((unsigned char)text[0]))
        return -1;
    value = strtoll(text, &end, 10);
    if (*end == 'b' && end[1] == '\0') {
        multiplier = 512;
    } else if (*end != '\0') {
        const char* unit = strchr(units, toupper((unsigned char)*end));
        int base = 1024, i;

        if (*end == '\0' || unit == NULL || *unit == '\0')
            return -1;
        if (end[1] == 'B' && end[2] == '\0')
            base = 1000;
        else if (end[1] != '\0')
            return -1;
        for (i = 0; i <= (int)(unit - units); i++) {
            if (multiplier > 0x7fffffffffffffffLL / base)
                return -1;
            multiplier *= base;
        }
    }
    if (multiplier != 1 && value > 0x7fffffffffffffffLL / multiplier)
        return -1;
    *out = value * multiplier;
    return 0;
}

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;
    int type = 0;

    if (id == ARGS_NUMBER || id == OPT_LINES)
        type = 1;
    else if (id == OPT_BYTES)
        type = 2;
    else if (id == OPT_LINE_BYTES)
        type = 3;
    else if (id == OPT_NUMBER)
        type = 4;
    if (type != 0) {
        if (s->typeGiven != 0 && s->typeGiven != type)
            s->conflict = 1;
        s->typeGiven = type;
    }

    if (id == ARGS_NUMBER) {
        // -NUM is -l NUM
        char* end;
        long long n = strtoll(value, &end, 10);

        if (*end != '\0' || n <= 0) {
            fprintf(stderr, PROGRAM ": invalid number of lines: '%s'\n", value);
            return -1;
        }
        s->mode = BY_LINES;
        s->size = n;
        return 0;
    }
    switch (id) {
        case OPT_SUFFIX_LENGTH: {
            char* end;
            long n = strtol(value, &end, 10);

            if (*value == '\0' || *end != '\0' || n < 0) {
                fprintf(stderr, PROGRAM ": invalid suffix length: '%s'\n", value);
                return -1;
            }
            s->suffixLength = (int)n;
            s->suffixGiven = 1;
            break;
        }
        case OPT_ADDITIONAL:
            if (strchr(value, '/') != NULL || strchr(value, '\\') != NULL) {
                fprintf(stderr, PROGRAM ": invalid suffix '%s', contains directory separator\n", value);
                return -1;
            }
            s->additional = value;
            break;
        case OPT_BYTES: case OPT_LINE_BYTES: case OPT_LINES: {
            long long n;

            if (parse_size(value, &n) != 0 || n <= 0) {
                fprintf(stderr, PROGRAM ": invalid number of %s: '%s'\n",
                        id == OPT_BYTES ? "bytes" : id == OPT_LINE_BYTES ? "bytes" : "lines", value);
                return -1;
            }
            s->mode = id == OPT_BYTES ? BY_BYTES : id == OPT_LINE_BYTES ? BY_LINE_BYTES : BY_LINES;
            s->size = n;
            break;
        }
        case OPT_NUMERIC_SHORT: s->style = 'd'; break;
        case OPT_NUMERIC:
        case OPT_HEX:
            s->style = id == OPT_HEX ? 'x' : 'd';
            if (value != NULL) {
                char* end;

                s->from = strtoll(value, &end, id == OPT_HEX ? 16 : 10);
                if (*value == '\0' || *end != '\0' || s->from < 0) {
                    fprintf(stderr, PROGRAM ": invalid suffix start: '%s'\n", value);
                    return -1;
                }
            }
            break;
        case OPT_HEX_SHORT: s->style = 'x'; break;
        case OPT_ELIDE: s->elide = 1; break;
        case OPT_NUMBER: {
            // N, K/N, l/N, l/K/N, r/N, r/K/N
            const char* p = value;
            char* end;

            s->mode = BY_CHUNKS;
            s->chunkKind = 'b';
            s->chunk = 0;
            if ((*p == 'l' || *p == 'r') && p[1] == '/') {
                s->chunkKind = *p;
                p += 2;
            }
            s->chunks = strtoll(p, &end, 10);
            if (*end == '/') {
                s->chunk = s->chunks;
                p = end + 1;
                s->chunks = strtoll(p, &end, 10);
                if (s->chunk <= 0 || s->chunk > s->chunks) {
                    fprintf(stderr, PROGRAM ": invalid chunk number: '%s'\n", value);
                    return -1;
                }
            }
            if (!isdigit((unsigned char)*p) || *end != '\0' || s->chunks <= 0) {
                fprintf(stderr, PROGRAM ": invalid number of chunks: '%s'\n", value);
                return -1;
            }
            break;
        }
        case OPT_SEPARATOR:
            if (value[0] == '\\' && value[1] == '0' && value[2] == '\0') {
                s->separator = 0;
            } else if (value[0] == '\0' || value[1] != '\0') {
                fprintf(stderr, PROGRAM ": multi-character separator '%s'\n", value);
                return -1;
            } else {
                s->separator = (unsigned char)value[0];
            }
            break;
        case OPT_VERBOSE: s->verbose = 1; break;
        case OPT_FILTER:
            fprintf(stderr, PROGRAM ": --filter is not supported\n");
            return -1;
        default: break;
    }
    return 0;
}

/* ---- the names of the output files ---------------------------------------------------------- */

typedef struct {
    const Settings* s;
    const char*     prefix;
    char            suffix[64];
    int             length;         /* of the suffix now */
    int             level;          /* how many times the numeric suffix has grown */
    long long       count;          /* files named so far */
    int             autoGrow;
    long long       startNumber;
} Names;

static int alphabet_last(char style) {
    return style == 'a' ? 'z' : style == 'x' ? 'f' : '9';
}

static int alphabet_first(char style) {
    return style == 'a' ? 'a' : '0';
}

static int next_char(char style, int c) {
    if (style == 'a')
        return c + 1;
    if (style == 'd')
        return c + 1;
    return c == '9' ? 'a' : c + 1;
}

/** The suffix is a number written in the alphabet (digits), and it is incremented by one. Returns 0, or -1 when exhausted. */
static int increment(Names* n) {
    int i = n->length;
    char style = n->s->style, last = (char)alphabet_last(style), first = (char)alphabet_first(style);

    while (i-- > 0) {
        if (n->suffix[i] != last) {
            n->suffix[i] = (char)next_char(style, n->suffix[i]);
            return 0;
        }
        n->suffix[i] = first;
    }
    return -1;
}

static void init_names(Names* n, const Settings* s, const char* prefix, long long chunks) {
    int i;

    memset(n, 0, sizeof *n);
    n->s = s;
    n->prefix = prefix;
    n->length = s->suffixLength > 0 ? s->suffixLength : 2;
    if (!s->suffixGiven && s->mode == BY_CHUNKS && chunks > 0) {
        // enough letters or digits for all the files
        long long capacity = 1;
        int base = s->style == 'a' ? 26 : s->style == 'x' ? 16 : 10, length = 0;

        while (capacity < chunks) {
            capacity *= base;
            length++;
        }
        n->length = length > 2 ? length : 2;
    }
    n->autoGrow = !s->suffixGiven;
    for (i = 0; i < n->length; i++)
        n->suffix[i] = (char)alphabet_first(s->style);
    n->suffix[n->length] = '\0';
    if (s->style != 'a' && s->from > 0) {
        long long k;

        for (k = 0; k < s->from; k++)
            increment(n);
    }
}

/** The name of the next file: prefix + suffix + additional suffix. Returns 0, or -1 when the suffixes are used up. */
static int next_name(Names* n, char* out, size_t size) {
    char shown[80];

    if (n->count > 0) {
        if (n->autoGrow) {
            // numeric suffixes grow: a digit that reaches the last one of the alphabet (after the marks that are
            // there already) is a mark, and the suffix goes on with two more digits: 89 is followed by 9000
            char last = (char)alphabet_last(n->s->style);

            increment(n);
            if (n->suffix[n->level] == last) {
                int k, newLength = n->length + 2;

                n->level++;
                if (newLength > 60)
                    return -1;
                for (k = 0; k < newLength; k++)
                    n->suffix[k] = (char)alphabet_first(n->s->style);
                for (k = 0; k < n->level; k++)
                    n->suffix[k] = last;
                n->length = newLength;
                n->suffix[n->length] = '\0';
            }
        } else if (increment(n) != 0) {
            fprintf(stderr, PROGRAM ": output file suffixes exhausted\n");
            return -1;
        }
    }
    n->count++;
    memcpy(shown, n->suffix, (size_t)n->length);
    shown[n->length] = '\0';
    snprintf(out, size, "%s%s%s", n->prefix, shown, n->s->additional != NULL ? n->s->additional : "");
    return 0;
}

/* ---- the output files ------------------------------------------------------------------------ */

typedef struct {
    const Settings* s;
    Names           names;
    FILE*           out;
    char            name[VPATH_MAX * 2];
    char            inputNative[VPATH_MAX];
    int             failed;
    int             toStdout;       /* -n K/N: only one piece, to standard output */
    long long       piece;          /* the number of the piece being written, from 1 */
    long long       wanted;         /* with toStdout: the number of the piece that is written */
    int             empty;
} Output;

static void close_output(Output* o) {
    if (o->out != NULL && o->out != stdout)
        fclose(o->out);
    o->out = NULL;
}

/** Starts the next file. Returns 0, or -1 after a message. */
static int open_next(Output* o) {
    char native[VPATH_MAX * 2];

    close_output(o);
    if (next_name(&o->names, o->name, sizeof o->name) != 0)
        return -1;
    o->piece++;
    if (o->s->verbose)
        printf("creating file '%s'\n", o->name);
    if (vpath_to_native(o->name, native, sizeof native) != 0) {
        fprintf(stderr, PROGRAM ": %s: Invalid argument\n", o->name);
        return -1;
    }
    if (o->inputNative[0] != '\0' && fs_same_file(o->inputNative, native)) {
        fprintf(stderr, PROGRAM ": '%s' would overwrite input; aborting\n", o->name);
        return -1;
    }
    o->out = fopen(native, "wb");
    if (o->out == NULL) {
        fprintf(stderr, PROGRAM ": %s: %s\n", o->name, fs_strerror(GetLastError()));
        return -1;
    }
    return 0;
}

static int put(Output* o, const unsigned char* data, size_t length) {
    if (o->toStdout)
        return fwrite(data, 1, length, stdout) == length ? 0 : -1;
    if (fwrite(data, 1, length, o->out) != length) {
        fprintf(stderr, PROGRAM ": %s: write error\n", o->name);
        return -1;
    }
    return 0;
}

/* ---- splitting ------------------------------------------------------------------------------- */

static int split_lines(Output* o, FILE* in, long long perFile) {
    unsigned char buffer[65536];
    long got;
    long long inFile = 0;
    int separator = o->s->separator;
    int started = 0;

    while ((got = text_read_some(in, buffer, sizeof buffer)) > 0) {
        long i = 0, from = 0;

        while (i < got) {
            if (!started) {
                if (open_next(o) != 0)
                    return -1;
                started = 1;
                inFile = 0;
            }
            if (buffer[i] == separator) {
                inFile++;
                if (put(o, buffer + from, (size_t)(i - from + 1)) != 0)
                    return -1;
                from = i + 1;
                if (inFile == perFile)
                    started = 0;
            }
            i++;
            if (!started && from < i) {
                // the file is complete; the rest of the buffer goes to the next one
                from = i;
            }
        }
        if (from < got && started && put(o, buffer + from, (size_t)(got - from)) != 0)
            return -1;
    }
    return got < 0 ? -1 : 0;
}

static int split_bytes(Output* o, FILE* in, long long perFile) {
    unsigned char buffer[65536];
    long got;
    long long inFile = 0;
    int started = 0;

    while ((got = text_read_some(in, buffer, sizeof buffer)) > 0) {
        long i = 0;

        while (i < got) {
            long long room, take;

            if (!started) {
                if (open_next(o) != 0)
                    return -1;
                started = 1;
                inFile = 0;
            }
            room = perFile - inFile;
            take = got - i < room ? got - i : room;
            if (put(o, buffer + i, (size_t)take) != 0)
                return -1;
            inFile += take;
            i += (long)take;
            if (inFile == perFile)
                started = 0;
        }
    }
    return got < 0 ? -1 : 0;
}

/** Records of at most `limit` bytes: a record that does not fit starts the next file; a longer one is cut. */
static int split_line_bytes(Output* o, FILE* in, long long limit) {
    unsigned char buffer[65536];
    long got;
    long long inFile = 0, recordLength = 0;
    unsigned char* record = NULL;
    size_t recordCapacity = 0;
    int started = 0;
    int separator = o->s->separator;

    while ((got = text_read_some(in, buffer, sizeof buffer)) > 0) {
        long i;

        for (i = 0; i < got; i++) {
            if ((size_t)recordLength + 1 > recordCapacity) {
                recordCapacity = recordCapacity == 0 ? 4096 : recordCapacity * 2;
                record = realloc(record, recordCapacity);
                if (record == NULL)
                    return -1;
            }
            record[recordLength++] = buffer[i];
            if (buffer[i] == separator) {
                // a whole record: into this file when it fits, else into the next
                size_t offset = 0;

                if (started && inFile + recordLength > limit) {
                    started = 0;
                }
                while ((long long)offset < recordLength) {
                    long long room, take;

                    if (!started) {
                        if (open_next(o) != 0)
                            return -1;
                        started = 1;
                        inFile = 0;
                    }
                    room = limit - inFile;
                    take = recordLength - (long long)offset < room ? recordLength - (long long)offset : room;
                    if (put(o, record + offset, (size_t)take) != 0)
                        return -1;
                    inFile += take;
                    offset += (size_t)take;
                    if (inFile == limit)
                        started = 0;
                }
                recordLength = 0;
            }
        }
    }
    if (recordLength > 0) {
        size_t offset = 0;

        if (started && inFile + recordLength > limit)
            started = 0;
        while ((long long)offset < recordLength) {
            long long room, take;

            if (!started) {
                if (open_next(o) != 0)
                    return -1;
                started = 1;
                inFile = 0;
            }
            room = limit - inFile;
            take = recordLength - (long long)offset < room ? recordLength - (long long)offset : room;
            if (put(o, record + offset, (size_t)take) != 0)
                return -1;
            inFile += take;
            offset += (size_t)take;
            if (inFile == limit)
                started = 0;
        }
    }
    free(record);
    return got < 0 ? -1 : 0;
}

/** -n: the file is read whole (its size has to be known); the pieces are cut from the bytes. */
static int split_chunks(Output* o, FILE* in, const char* inputName, long long size) {
    unsigned char* data = NULL;
    size_t length = 0, position = 0;
    long long chunks = o->s->chunks, i;
    int separator = o->s->separator;

    (void)inputName;
    data = text_slurp(in, &length);
    if (data == NULL)
        return -1;
    (void)size;

    if (o->s->chunkKind == 'r') {
        // round robin: record j goes to piece j mod N
        long long record = 0;
        size_t start = 0, k;
        FILE** files = NULL;

        if (!o->toStdout) {
            files = calloc((size_t)chunks, sizeof *files);
            if (files == NULL)
                return -1;
        }
        for (k = 0; k <= length; k++) {
            if (k == length ? k > start : data[k] == separator) {
                long long piece = record % chunks + 1;

                if (!o->toStdout) {
                    if (files[piece - 1] == NULL) {
                        // the files are opened in the order of the pieces when first needed
                        long long p;

                        for (p = 0; p <= piece - 1; p++) {
                            if (files[p] == NULL) {
                                if (open_next(o) != 0)
                                    return -1;
                                files[p] = o->out;
                                o->out = NULL;
                            }
                        }
                    }
                    fwrite(data + start, 1, k - start + (k < length ? 1 : 0), files[piece - 1]);
                } else if (piece == o->wanted) {
                    fwrite(data + start, 1, k - start + (k < length ? 1 : 0), stdout);
                }
                record++;
                start = k + 1;
            }
        }
        if (!o->toStdout) {
            for (i = 0; i < chunks; i++) {
                if (files[i] == NULL && !o->s->elide) {
                    if (open_next(o) != 0)
                        return -1;
                    files[i] = o->out;
                    o->out = NULL;
                }
                if (files[i] != NULL)
                    fclose(files[i]);
            }
            free(files);
        }
        free(data);
        return 0;
    }

    {
        long long size = (long long)length;
        long long chunkSize = size / chunks, rest = size % chunks;

        if (o->s->chunkKind == 'b') {
            if (o->toStdout) {
                // K/N: chunk K of the same cut as the files of -n N
                long long each = chunkSize > 0 ? chunkSize : 1;
                long long start = (o->wanted - 1) * each, end;

                if (start > size)
                    start = size;
                end = o->wanted == chunks ? size : start + each;
                if (end > size)
                    end = size;
                if (end > start)
                    fwrite(data + start, 1, (size_t)(end - start), stdout);
            } else {
                // the first N-1 files get size/N bytes (at least 1), the last one all that is left
                long long each = chunkSize > 0 ? chunkSize : 1;

                for (i = 0; i < chunks; i++) {
                    long long start = i * each, end;

                    if (start > size)
                        start = size;
                    end = i + 1 == chunks ? size : start + each;
                    if (end > size)
                        end = size;
                    if (end > start || !o->s->elide) {
                        if (open_next(o) != 0)
                            return -1;
                        if (end > start && put(o, data + start, (size_t)(end - start)) != 0)
                            return -1;
                    }
                }
            }
        } else {
            // whole lines: a line goes to the chunk that is current, and after it the chunk moves on while the
            // last byte of the line is at or beyond the end of the chunk (k * size/N - 1), but never past the last
            long long k = 1;
            size_t start = 0, at;
            int opened = 0;                 /* the number of files opened (names taken) */

            for (at = 0; at < length;) {
                size_t end = at;

                while (end < length && data[end] != separator)
                    end++;
                if (end < length)
                    end++;                  /* the separator belongs to the line */
                if (o->toStdout) {
                    if (k == o->wanted)
                        fwrite(data + at, 1, end - at, stdout);
                } else {
                    while (opened < k) {
                        // the files before this one that got no line are made (empty) in their order, unless elided
                        if (opened + 1 < k && o->s->elide) {
                            opened++;
                            continue;
                        }
                        if (open_next(o) != 0)
                            return -1;
                        opened++;
                        if (opened < k)
                            close_output(o);
                    }
                    if (put(o, data + at, end - at) != 0)
                        return -1;
                }
                while (k < chunks && (long long)end - 1 >= k * chunkSize - 1)
                    k++;
                at = end;
            }
            (void)start;
            (void)rest;
            if (!o->toStdout) {
                // the files that are left get nothing
                while (opened < chunks) {
                    if (!o->s->elide) {
                        if (open_next(o) != 0)
                            return -1;
                        close_output(o);
                    }
                    opened++;
                }
            }
        }
    }
    free(data);
    return 0;
}

int main(int argc, char* argv[]) {
    Settings s;
    const char** operands;
    int count, result, status = 0;
    const char* inputName = "-";
    const char* prefix = "x";
    FILE* in;
    Output output;
    long long size = 0;

    _setmode(_fileno(stdout), _O_BINARY);
    memset(&s, 0, sizeof s);
    s.mode = BY_LINES;
    s.size = 1000;
    s.style = 'a';
    s.separator = '\n';
    result = args_parse(&spec, argc, argv, on_option, &s, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (s.conflict) {
        fprintf(stderr, PROGRAM ": cannot split in more than one way\n");
        args_usage_error(&spec);
        return 1;
    }
    if (count > 2) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[2]);
        args_usage_error(&spec);
        return 1;
    }
    if (count >= 1)
        inputName = operands[0];
    if (count == 2)
        prefix = operands[1];

    text_binary_streams();
    if (text_open(PROGRAM, inputName, 1, &in) != 0)
        return 1;

    memset(&output, 0, sizeof output);
    output.s = &s;
    output.toStdout = s.mode == BY_CHUNKS && s.chunk > 0;
    output.wanted = s.chunk;
    if (strcmp(inputName, "-") != 0)
        vpath_to_native(inputName, output.inputNative, sizeof output.inputNative);
    if (s.mode == BY_CHUNKS && in == stdin) {
        DWORD type = GetFileType(GetStdHandle(STD_INPUT_HANDLE));

        if (type != FILE_TYPE_DISK) {
            fprintf(stderr, PROGRAM ": -: cannot determine file size\n");
            return 1;
        }
    }
    init_names(&output.names, &s, prefix, s.chunks);
    (void)size;

    switch (s.mode) {
        case BY_LINES:      result = split_lines(&output, in, s.size); break;
        case BY_BYTES:      result = split_bytes(&output, in, s.size); break;
        case BY_LINE_BYTES: result = split_line_bytes(&output, in, s.size); break;
        default:            result = split_chunks(&output, in, inputName, 0); break;
    }
    if (result != 0)
        status = 1;
    close_output(&output);
    if (in != stdin)
        fclose(in);
    return fflush(stdout) != 0 ? 1 : status;
}
