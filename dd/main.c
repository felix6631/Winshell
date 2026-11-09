/*
 * dd: copies a file, converting and formatting it as the operands say, as GNU dd does: if= of= bs= ibs= obs= cbs=
 * count= skip= seek= status= conv= (notrunc noerror sync swab ucase lcase ascii ebcdic ibm block unblock excl
 * nocreate fsync fdatasync) and the flags fullblock, count_bytes, skip_bytes, seek_bytes (the other flags are accepted
 * and have no meaning here). The counts of records and the speed go to standard error.
 */

#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <windows.h>
#include "fsutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "dd"

#include "tables.inc"

static void print_usage(void) {
    puts(
#include "help.inc"
    );
}

typedef unsigned long long u64;

enum {
    C_NOTRUNC = 1, C_NOERROR = 2, C_SYNC = 4, C_SWAB = 8, C_UCASE = 16, C_LCASE = 32, C_ASCII = 64, C_EBCDIC = 128,
    C_IBM = 256, C_BLOCK = 512, C_UNBLOCK = 1024, C_EXCL = 2048, C_NOCREATE = 4096, C_FSYNC = 8192, C_FDATASYNC = 16384,
    C_SPARSE = 32768
};

typedef struct {
    const char* input;
    const char* output;
    u64         ibs, obs, cbs;
    int         ibsGiven, obsGiven, bsGiven;
    u64         count, skip, seek;
    int         haveCount;
    int         conv;
    int         fullblock, countBytes, skipBytes, seekBytes;
    int         status;             // 0 default, 1 noxfer, 2 none, 3 progress
} Settings;

static void usage_error(void) {
    fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM);
}

/** One number as dd reads it: digits, a suffix, and further factors after x. Returns 0, or -1 (invalid). */
static int parse_number(const char* text, u64* out) {
    const char* p = text;
    u64 total = 1;
    int first = 1;

    for (;;) {
        u64 value = 0, factor = 1;
        int digits = 0, overflow = 0;
        const char* start = p;

        while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
            p++;
        if (*p == '-')
            return -1;
        if (*p == '+')
            p++;
        for (; *p >= '0' && *p <= '9'; p++) {
            unsigned d = (unsigned)(*p - '0');

            digits++;
            if (value > (ULLONG_MAX - d) / 10)
                overflow = 1;
            else
                value = value * 10 + d;
        }
        if (digits == 0) {
            // a suffix alone is one of it: "K" means 1K
            if (!strchr("bcEGkKMPTwYZ", *p) || *p == '\0')
                return -1;
            value = 1;
        }
        switch (*p) {
        case 'b': factor = 512; p++; break;
        case 'c': factor = 1; p++; break;
        case 'w': factor = 2; p++; break;
        case 'k': case 'K': case 'M': case 'G': case 'T': case 'P': case 'E': case 'Z': case 'Y': {
            int power = *p == 'k' || *p == 'K' ? 1 : *p == 'M' ? 2 : *p == 'G' ? 3 : *p == 'T' ? 4 : *p == 'P' ? 5 :
                        *p == 'E' ? 6 : *p == 'Z' ? 7 : 8;
            u64 base = 1024;
            int i;

            p++;
            if (*p == 'B') {
                base = 1000;
                p++;
            } else if (p[0] == 'i' && p[1] == 'B') {
                p += 2;
            }
            for (i = 0; i < power; i++) {
                if (factor > ULLONG_MAX / base) {
                    overflow = 1;
                    break;
                }
                factor *= base;
            }
            break;
        }
        default: break;
        }
        (void)start;
        if (overflow || (factor > 1 && value > ULLONG_MAX / factor))
            return -1;
        value *= factor;
        if (*p == 'x') {
            if (value == 0)
                fprintf(stderr, PROGRAM ": warning: '%s' is a zero multiplier; use '00x' if that is intended\n", text);
            if (total > 0 && value > 0 && total > ULLONG_MAX / value)
                return -1;
            total = first ? value : total * value;
            first = 0;
            p++;
            continue;
        }
        if (*p != '\0')
            return -1;
        if (!first) {
            if (value != 0 && total > ULLONG_MAX / value)
                return -1;
            total *= value;
        } else {
            total = value;
        }
        break;
    }
    *out = total;
    return 0;
}

static int parse_bytes_arg(const char* name, const char* text, u64* out, int allowZero) {
    if (parse_number(text, out) != 0 || (!allowZero && *out == 0)) {
        fprintf(stderr, PROGRAM ": invalid number: '%s'\n", text);
        (void)name;
        return -1;
    }
    return 0;
}

static int set_conv(Settings* s, const char* list) {
    char buffer[256];
    char* token;
    char* next = buffer;
    int more = 1;

    snprintf(buffer, sizeof buffer, "%s", list);
    while (more) {
        static const struct { const char* name; int bit; } names[] = {
            { "notrunc", C_NOTRUNC }, { "noerror", C_NOERROR }, { "sync", C_SYNC }, { "swab", C_SWAB },
            { "ucase", C_UCASE }, { "lcase", C_LCASE }, { "ascii", C_ASCII }, { "ebcdic", C_EBCDIC }, { "ibm", C_IBM },
            { "block", C_BLOCK }, { "unblock", C_UNBLOCK }, { "excl", C_EXCL }, { "nocreat", C_NOCREATE }, { "sparse", C_SPARSE },
            { "fsync", C_FSYNC }, { "fdatasync", C_FDATASYNC }, { NULL, 0 }
        };
        int i, found = 0;

        token = next;
        next = strchr(token, ',');
        if (next != NULL)
            *next++ = '\0';
        else
            more = 0;

        for (i = 0; names[i].name != NULL; i++)
            if (strcmp(token, names[i].name) == 0) {
                s->conv |= names[i].bit;
                found = 1;
            }
        if (!found) {
            fprintf(stderr, PROGRAM ": invalid conversion: '%s'\n", token);
            usage_error();
            return -1;
        }
    }
    return 0;
}

static int set_flags(Settings* s, const char* list, int isInput) {
    char buffer[256];
    char* token;
    char* next = buffer;
    int more = 1;
    static const char* const ignored[] = { "append", "binary", "cio", "direct", "directory", "dsync", "noatime", "nocache",
                                           "noctty", "nofollow", "nolinks", "nonblock", "sync", "text", "excl", NULL };

    snprintf(buffer, sizeof buffer, "%s", list);
    while (more) {
        int i, found = 0;

        token = next;
        next = strchr(token, ',');
        if (next != NULL)
            *next++ = '\0';
        else
            more = 0;
        if (strcmp(token, "fullblock") == 0 && isInput) {
            s->fullblock = 1;
            continue;
        }
        if (strcmp(token, "count_bytes") == 0 && isInput) {
            s->countBytes = 1;
            continue;
        }
        if (strcmp(token, "skip_bytes") == 0 && isInput) {
            s->skipBytes = 1;
            continue;
        }
        if (strcmp(token, "seek_bytes") == 0 && !isInput) {
            s->seekBytes = 1;
            continue;
        }
        for (i = 0; ignored[i] != NULL; i++)
            if (strcmp(token, ignored[i]) == 0)
                found = 1;
        if (!found) {
            fprintf(stderr, PROGRAM ": invalid %s flag: '%s'\n", isInput ? "input" : "output", token);
            usage_error();
            return -1;
        }
    }
    return 0;
}

/* ---- the copy ------------------------------------------------------------------------------ */

typedef struct {
    int     in, out;
    const char* inName;
    const char* outName;
    u64     rFull, rPartial, wFull, wPartial, truncated;
    u64     bytesWritten;
    u64     bytesRead;
    int     sparse;
    int     sparsePending;
    unsigned char* obuf;
    size_t  obufLength;
    u64     obs;
    int     failed;
    int     col;                    // block / unblock
    u64     pendingSpaces;
    int     swabHeld;
    unsigned char swabByte;
    unsigned char table[256];
    int     translate;
} Copy;

static void write_error(const Copy* c) {
    fprintf(stderr, PROGRAM ": error writing '%s': %s\n", c->outName != NULL ? c->outName : "standard output", strerror(errno));
}

static int write_all(Copy* c, const unsigned char* data, size_t length) {
    if (c->sparse && length == c->obs) {            // a block of NUL bytes is skipped over, not written
        size_t k;

        for (k = 0; k < length && data[k] == 0; k++)
            ;
        if (k == length && _lseeki64(c->out, (__int64)length, SEEK_CUR) >= 0) {
            c->sparsePending = 1;
            c->bytesWritten += (u64)length;
            return 0;
        }
    }
    c->sparsePending = 0;
    while (length > 0) {
        int n = _write(c->out, data, (unsigned)(length > 1048576 ? 1048576 : length));

        if (n <= 0) {
            write_error(c);
            c->failed = 1;
            return -1;
        }
        data += n;
        length -= (size_t)n;
        c->bytesWritten += (u64)n;
    }
    return 0;
}

/** Appends to the output buffer and writes the whole blocks of it. */
static int emit(Copy* c, const unsigned char* data, size_t length) {
    while (length > 0) {
        size_t room = (size_t)c->obs - c->obufLength, take = length < room ? length : room;

        memcpy(c->obuf + c->obufLength, data, take);
        c->obufLength += take;
        data += take;
        length -= take;
        if (c->obufLength == c->obs) {
            if (write_all(c, c->obuf, c->obufLength) != 0)
                return -1;
            c->wFull++;
            c->obufLength = 0;
        }
    }
    return 0;
}

static int emit_byte(Copy* c, unsigned char b) {
    return emit(c, &b, 1);
}

static int block_convert(Copy* c, const Settings* s, const unsigned char* data, size_t length) {
    size_t i;

    for (i = 0; i < length; i++) {
        if (s->conv & C_BLOCK) {
            if (data[i] == '\n') {
                for (; (u64)c->col < s->cbs; c->col++)
                    if (emit_byte(c, ' ') != 0)
                        return -1;
                c->col = 0;
            } else {
                if ((u64)c->col == s->cbs)
                    c->truncated++;
                else if ((u64)c->col < s->cbs && emit_byte(c, data[i]) != 0)
                    return -1;
                c->col++;
            }
        } else {                                    // unblock
            if ((u64)c->col++ >= s->cbs) {
                c->col = 0;
                c->pendingSpaces = 0;
                if (emit_byte(c, '\n') != 0)
                    return -1;
                i--;                                // this byte again
            } else if (data[i] == ' ') {
                c->pendingSpaces++;
            } else {
                for (; c->pendingSpaces > 0; c->pendingSpaces--)
                    if (emit_byte(c, ' ') != 0)
                        return -1;
                if (emit_byte(c, data[i]) != 0)
                    return -1;
            }
        }
    }
    return 0;
}

/** Reads up to size bytes: one read, or until full with iflag=fullblock. Returns the count, 0 at the end, -1 on an error. */
static long read_block(Copy* c, unsigned char* buffer, size_t size, int fullblock) {
    size_t got = 0;

    do {
        int n = _read(c->in, buffer + got, (unsigned)(size - got));

        if (n < 0)
            return -1;
        if (n == 0)
            break;
        got += (size_t)n;
    } while (fullblock && got < size);
    return (long)got;
}

static char* human(char* out, size_t size, double value, int base) {
    static const char* const si[] = { "B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
    static const char* const iec[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB" };
    int unit = 0;

    while (value >= base && unit < 8) {
        value /= base;
        unit++;
    }
    if (unit > 0 && value < 10)
        snprintf(out, size, "%.1f %s", value, base == 1000 ? si[unit] : iec[unit]);
    else
        snprintf(out, size, "%.0f %s", value, base == 1000 ? si[unit] : iec[unit]);
    return out;
}

static void print_stats(const Copy* c, int status, double seconds) {
    if (status == 2)
        return;
    fprintf(stderr, "%llu+%llu records in\n%llu+%llu records out\n", c->rFull, c->rPartial, c->wFull, c->wPartial);
    if (c->truncated > 0)
        fprintf(stderr, "%llu truncated record%s\n", c->truncated, c->truncated == 1 ? "" : "s");
    if (status == 1)
        return;
    {
        char a[40], b[40], rate[40];
        double bytes = (double)c->bytesWritten;
        char speed[64];

        if (seconds > 0)
            human(rate, sizeof rate, bytes / seconds, 1000);
        else
            snprintf(rate, sizeof rate, "Infinity B");
        snprintf(speed, sizeof speed, "%s/s", rate);
        human(a, sizeof a, bytes, 1000);
        human(b, sizeof b, bytes, 1024);
        if (c->bytesWritten < 1000)
            fprintf(stderr, "%llu byte%s copied, %g s, %s\n", c->bytesWritten, c->bytesWritten == 1 ? "" : "s", seconds, speed);
        else
            fprintf(stderr, "%llu bytes (%s, %s) copied, %g s, %s\n", c->bytesWritten, a, b, seconds, speed);
    }
}

static int is_seekable(int fd) {
    struct _stat64 st;

    return _fstat64(fd, &st) == 0 && (st.st_mode & _S_IFREG) != 0;
}

int main(int argc, char* argv[]) {
    Settings s;
    Copy c;
    int i, simple, status = 0;
    unsigned char* ibuf;
    u64 records = 0;
    LARGE_INTEGER startTicks, endTicks, frequency;
    u64 ibsFromBs = 0;
    unsigned char caseTable[256];

    memset(&s, 0, sizeof s);
    s.ibs = s.obs = 512;
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            printf("dd (Winshell) %s\n", VERSION);
            return 0;
        }
    }
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);

    for (i = 1; i < argc; i++) {
        char* eq = strchr(argv[i], '=');
        char name[32];
        const char* value;

        if (eq == NULL || (size_t)(eq - argv[i]) >= sizeof name) {
            fprintf(stderr, PROGRAM ": unrecognized operand '%s'\n", argv[i]);
            usage_error();
            return 1;
        }
        snprintf(name, sizeof name, "%.*s", (int)(eq - argv[i]), argv[i]);
        value = eq + 1;
        if (strcmp(name, "if") == 0) {
            s.input = value;
        } else if (strcmp(name, "of") == 0) {
            s.output = value;
        } else if (strcmp(name, "ibs") == 0) {
            if (parse_bytes_arg(name, value, &s.ibs, 0) != 0)
                return 1;
            s.ibsGiven = 1;
        } else if (strcmp(name, "obs") == 0) {
            if (parse_bytes_arg(name, value, &s.obs, 0) != 0)
                return 1;
            s.obsGiven = 1;
        } else if (strcmp(name, "bs") == 0) {
            if (parse_bytes_arg(name, value, &ibsFromBs, 0) != 0)
                return 1;
            s.bsGiven = 1;
        } else if (strcmp(name, "cbs") == 0) {
            if (parse_bytes_arg(name, value, &s.cbs, 0) != 0)
                return 1;
        } else if (strcmp(name, "count") == 0) {
            if (parse_bytes_arg(name, value, &s.count, 1) != 0)
                return 1;
            s.haveCount = 1;
        } else if (strcmp(name, "skip") == 0 || strcmp(name, "iseek") == 0) {
            if (parse_bytes_arg(name, value, &s.skip, 1) != 0)
                return 1;
        } else if (strcmp(name, "seek") == 0 || strcmp(name, "oseek") == 0) {
            if (parse_bytes_arg(name, value, &s.seek, 1) != 0)
                return 1;
        } else if (strcmp(name, "conv") == 0) {
            if (set_conv(&s, value) != 0)
                return 1;
        } else if (strcmp(name, "iflag") == 0) {
            if (set_flags(&s, value, 1) != 0)
                return 1;
        } else if (strcmp(name, "oflag") == 0) {
            if (set_flags(&s, value, 0) != 0)
                return 1;
        } else if (strcmp(name, "status") == 0) {
            if (strcmp(value, "none") == 0) {
                s.status = 2;
            } else if (strcmp(value, "noxfer") == 0) {
                s.status = 1;
            } else if (strcmp(value, "progress") == 0) {
                s.status = 3;
            } else {
                fprintf(stderr, PROGRAM ": invalid status level: '%s'\n", value);
                usage_error();
                return 1;
            }
        } else {
            fprintf(stderr, PROGRAM ": unrecognized operand '%s'\n", argv[i]);
            usage_error();
            return 1;
        }
    }
    if (s.bsGiven)
        s.ibs = s.obs = ibsFromBs;
    if ((s.conv & C_UCASE) && (s.conv & C_LCASE)) {
        fprintf(stderr, PROGRAM ": cannot combine lcase and ucase\n");
        usage_error();
        return 1;
    }
    if (((s.conv & C_ASCII) != 0) + ((s.conv & C_EBCDIC) != 0) + ((s.conv & C_IBM) != 0) > 1) {
        fprintf(stderr, PROGRAM ": cannot combine any two of {ascii,ebcdic,ibm}\n");
        usage_error();
        return 1;
    }
    if ((s.conv & C_BLOCK) && (s.conv & C_UNBLOCK)) {
        fprintf(stderr, PROGRAM ": cannot combine block and unblock\n");
        usage_error();
        return 1;
    }
    if (s.cbs == 0)
        s.conv &= ~(C_BLOCK | C_UNBLOCK);
    if (s.ibs > 1u << 30 || s.obs > 1u << 30) {
        fprintf(stderr, PROGRAM ": memory exhausted by input buffer of size %llu bytes\n", s.ibs > s.obs ? s.ibs : s.obs);
        return 1;
    }

    memset(&c, 0, sizeof c);
    c.in = 0;
    c.out = 1;
    c.inName = s.input;
    c.outName = s.output;
    c.obs = s.obs;
    c.sparse = (s.conv & C_SPARSE) != 0 && s.output != NULL;

    // the translation: ascii, then the case, then ebcdic or ibm
    {
        int b;

        for (b = 0; b < 256; b++) {
            unsigned char v = (unsigned char)b;

            if (s.conv & C_ASCII)
                v = ebcdicToAscii[v];
            if (s.conv & C_UCASE)
                v = (v >= 'a' && v <= 'z') ? (unsigned char)(v - 32) : v;
            if (s.conv & C_LCASE)
                v = (v >= 'A' && v <= 'Z') ? (unsigned char)(v + 32) : v;
            if (s.conv & C_EBCDIC)
                v = asciiToEbcdic[v];
            if (s.conv & C_IBM)
                v = asciiToIbm[v];
            caseTable[b] = v;
        }
        c.translate = (s.conv & (C_ASCII | C_UCASE | C_LCASE | C_EBCDIC | C_IBM)) != 0;
        memcpy(c.table, caseTable, sizeof c.table);
    }

    if (s.input != NULL) {
        char native[VPATH_MAX];

        if (vpath_to_native(s.input, native, sizeof native) != 0 || (c.in = _open(native, _O_RDONLY | _O_BINARY)) < 0) {
            fprintf(stderr, PROGRAM ": failed to open '%s': %s\n", s.input, strerror(errno));
            return 1;
        }
    }
    if (s.output != NULL) {
        char native[VPATH_MAX];
        int flags = _O_WRONLY | _O_BINARY;

        if (!(s.conv & C_NOCREATE))
            flags |= _O_CREAT;
        if (s.conv & C_EXCL)
            flags |= _O_EXCL;
        if (!(s.conv & C_NOTRUNC) && (s.seek == 0 || s.seekBytes))
            flags |= _O_TRUNC;
        if (vpath_to_native(s.output, native, sizeof native) != 0 || (c.out = _open(native, flags, _S_IREAD | _S_IWRITE)) < 0) {
            fprintf(stderr, PROGRAM ": failed to open '%s': %s\n", s.output, strerror(errno));
            return 1;
        }
    } else {
        _setmode(1, _O_BINARY);
    }

    // skip: seek in a file, read and drop otherwise
    if (s.skip > 0) {
        u64 bytes = s.skipBytes ? s.skip : s.skip * s.ibs;

        if (is_seekable(c.in)) {
            if (_lseeki64(c.in, (__int64)bytes, SEEK_SET) < 0) {
                fprintf(stderr, PROGRAM ": cannot skip to specified offset\n");
                return 1;
            }
        } else {
            unsigned char* drop = malloc((size_t)(s.ibs > 65536 ? 65536 : s.ibs));
            u64 left = bytes;

            if (drop == NULL)
                return 1;
            while (left > 0) {
                long n = read_block(&c, drop, (size_t)(left < (s.ibs > 65536 ? 65536 : s.ibs) ? left : (s.ibs > 65536 ? 65536 : s.ibs)), 0);

                if (n <= 0) {
                    fprintf(stderr, PROGRAM ": cannot skip to specified offset\n");
                    break;
                }
                left -= (u64)n;
            }
            free(drop);
        }
    }
    if (s.seek > 0) {
        u64 bytes = s.seekBytes ? s.seek : s.seek * s.obs;

        if (_lseeki64(c.out, (__int64)bytes, SEEK_SET) < 0) {
            fprintf(stderr, PROGRAM ": cannot seek: %s\n", "Illegal seek");
            return 1;
        }
        if (!(s.conv & C_NOTRUNC) && !s.seekBytes && s.output != NULL)
            _chsize_s(c.out, (__int64)bytes);                        // the file ends at the offset the output starts
    }

    ibuf = malloc((size_t)s.ibs + 2);
    c.obuf = malloc((size_t)s.obs);
    if (ibuf == NULL || c.obuf == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return 1;
    }
    simple = s.ibs == s.obs && !(s.conv & (C_BLOCK | C_UNBLOCK));
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&startTicks);

    for (;;) {
        long n;
        size_t length;
        u64 want = s.ibs;

        if (s.haveCount) {
            if (s.countBytes) {
                if (c.bytesRead >= s.count)
                    break;
                if (s.count - c.bytesRead < want)
                    want = s.count - c.bytesRead;
            } else if (records >= s.count) {
                break;
            }
        }
        n = read_block(&c, ibuf, (size_t)want, s.fullblock);
        if (n < 0) {
            fprintf(stderr, PROGRAM ": error reading '%s': %s\n", s.input != NULL ? s.input : "standard input", strerror(errno));
            if (!(s.conv & C_NOERROR)) {
                status = 1;
                break;
            }
            n = 0;
            if (!(s.conv & C_SYNC)) {
                c.rPartial++;
                records++;
                continue;
            }
        }
        if (n == 0 && !(n < 0) && want > 0 && !(s.conv & C_NOERROR)) {
            break;
        }
        if (n == 0)
            break;
        length = (size_t)n;
        c.bytesRead += (u64)n;
        records++;
        if (length == s.ibs)
            c.rFull++;
        else
            c.rPartial++;
        if ((s.conv & C_SYNC) && length < s.ibs) {
            memset(ibuf + length, (s.conv & (C_BLOCK | C_UNBLOCK)) ? ' ' : 0, (size_t)s.ibs - length);
            length = (size_t)s.ibs;
        }
        if (c.translate) {
            size_t k;

            for (k = 0; k < length; k++)
                ibuf[k] = c.table[ibuf[k]];
        }
        if (s.conv & C_SWAB) {
            // pairs of bytes are swapped; an odd last byte waits for the first byte of the next block
            unsigned char* start = ibuf;
            size_t k, total = length;

            if (c.swabHeld) {
                memmove(ibuf + 1, ibuf, length);
                ibuf[0] = c.swabByte;
                total++;
                c.swabHeld = 0;
            }
            if (total & 1) {
                c.swabByte = start[total - 1];
                c.swabHeld = 1;
                total--;
            }
            for (k = 0; k + 1 < total; k += 2) {
                unsigned char t = start[k];

                start[k] = start[k + 1];
                start[k + 1] = t;
            }
            length = total;
        }
        if (s.conv & (C_BLOCK | C_UNBLOCK)) {
            if (block_convert(&c, &s, ibuf, length) != 0) {
                status = 1;
                break;
            }
        } else if (simple) {
            if (length > 0) {
                if (write_all(&c, ibuf, length) != 0) {
                    status = 1;
                    break;
                }
                if (length == s.obs)
                    c.wFull++;
                else
                    c.wPartial++;
            }
        } else if (emit(&c, ibuf, length) != 0) {
            status = 1;
            break;
        }
    }
    if (!status) {
        if (c.swabHeld && emit(&c, &c.swabByte, 1) != 0)
            status = 1;
        if (!status && (s.conv & C_BLOCK) && c.col > 0 && (u64)c.col < s.cbs) {
            for (; (u64)c.col < s.cbs; c.col++)
                emit_byte(&c, ' ');
        }
        if (!status && (s.conv & C_UNBLOCK) && c.col > 0)
            emit_byte(&c, '\n');
        if (!status && c.obufLength > 0) {
            if (write_all(&c, c.obuf, c.obufLength) != 0)
                status = 1;
            else
                c.wPartial++;
        }
    }
    if (c.sparsePending && c.sparse) {                // the file ends with the skipped blocks: it has their length
        __int64 position = _lseeki64(c.out, 0, SEEK_CUR);

        if (position >= 0)
            _chsize_s(c.out, position);
    }
    if (c.failed)
        status = 1;
    if (s.conv & (C_FSYNC | C_FDATASYNC))
        _commit(c.out);
    QueryPerformanceCounter(&endTicks);
    print_stats(&c, s.status, (double)(endTicks.QuadPart - startTicks.QuadPart) / (double)frequency.QuadPart);
    if (s.output != NULL)
        _close(c.out);
    return status;
}
