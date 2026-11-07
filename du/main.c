/*
 * du: estimates the space files use, as GNU du does. The space is the allocated size of the file system (what
 * Windows reports as AllocationSize; --apparent-size and -b use the sizes of the files instead). A directory
 * is printed after what is in it, hard links are counted once (unless -l), and links to directories (junctions,
 * symbolic links) are not entered unless -L.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "du"

static void print_usage(void) {
    puts("Usage: du [OPTION]... [FILE]...\n"
         "Summarize disk usage of the set of FILEs, recursively for directories.\n"
         "\n"
         "  -0, --null            end each output line with NUL, not newline\n"
         "  -a, --all             write counts for all files, not just directories\n"
         "      --apparent-size   print apparent sizes, rather than disk usage\n"
         "  -B, --block-size=SIZE  scale sizes by SIZE before printing them\n"
         "  -b, --bytes           equivalent to '--apparent-size --block-size=1'\n"
         "  -c, --total           produce a grand total\n"
         "  -d, --max-depth=N     print the total for a directory only if it is N or fewer levels\n"
         "                          below the command line argument;  --max-depth=0 is the same as --summarize\n"
         "      --exclude=PATTERN  exclude files that match PATTERN\n"
         "  -h, --human-readable  print sizes in human readable format (e.g., 1K 234M 2G)\n"
         "      --si              like -h, but use powers of 1000 not 1024\n"
         "  -k                    like --block-size=1K\n"
         "  -L, --dereference     dereference all symbolic links\n"
         "  -l, --count-links     count sizes many times if hard linked\n"
         "  -m                    like --block-size=1M\n"
         "  -P, --no-dereference  don't follow any symbolic links (this is the default)\n"
         "  -S, --separate-dirs   for directories do not include size of subdirectories\n"
         "  -s, --summarize       display only a total for each argument\n"
         "  -x, --one-file-system  skip directories on different file systems\n"
         "      --help            display this help and exit\n"
         "      --version         output version information and exit\n"
         "\n"
         "Display values are in units of the first available SIZE from --block-size,\n"
         "and the DU_BLOCK_SIZE, BLOCK_SIZE and BLOCKSIZE environment variables.\n"
         "Otherwise, units default to 1024 bytes (or 512 if POSIXLY_CORRECT is set).");
}

enum { OPT_NULL = 1, OPT_ALL, OPT_APPARENT, OPT_BLOCK_SIZE, OPT_BYTES, OPT_TOTAL, OPT_MAX_DEPTH, OPT_EXCLUDE, OPT_HUMAN, OPT_SI,
       OPT_K, OPT_DEREFERENCE, OPT_COUNT_LINKS, OPT_M, OPT_PHYSICAL, OPT_SEPARATE, OPT_SUMMARIZE, OPT_ONE_FS };

static const ArgsOption options[] = {
    { OPT_NULL,        '0', "null",            ARGS_NONE },
    { OPT_ALL,         'a', "all",             ARGS_NONE },
    { OPT_APPARENT,    0,   "apparent-size",   ARGS_NONE },
    { OPT_BLOCK_SIZE,  'B', "block-size",      ARGS_REQUIRED },
    { OPT_BYTES,       'b', "bytes",           ARGS_NONE },
    { OPT_TOTAL,       'c', "total",           ARGS_NONE },
    { OPT_MAX_DEPTH,   'd', "max-depth",       ARGS_REQUIRED },
    { OPT_EXCLUDE,     0,   "exclude",         ARGS_REQUIRED },
    { OPT_HUMAN,       'h', "human-readable",  ARGS_NONE },
    { OPT_SI,          0,   "si",              ARGS_NONE },
    { OPT_K,           'k', NULL,              ARGS_NONE },
    { OPT_DEREFERENCE, 'L', "dereference",     ARGS_NONE },
    { OPT_COUNT_LINKS, 'l', "count-links",     ARGS_NONE },
    { OPT_M,           'm', NULL,              ARGS_NONE },
    { OPT_PHYSICAL,    'P', "no-dereference",  ARGS_NONE },
    { OPT_SEPARATE,    'S', "separate-dirs",   ARGS_NONE },
    { OPT_SUMMARIZE,   's', "summarize",       ARGS_NONE },
    { OPT_ONE_FS,      'x', "one-file-system", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int       terminator, all, apparent, total, human, si, dereference, countLinks, separate, summarize, oneFileSystem;
    long long blockSize;
    char      blockSuffix[8];       /* printed after the number when -B has a unit and no digits: -BM gives 1M */
    long long maxDepth;             /* -1: no limit */
    const char* excludes[16];
    int       excludeCount;
} Settings;

/** Reads SIZE of -B: digits with a unit (K M G T P E Z Y, KB MB ...). Returns 0, or -1. */
static int parse_block_size(const char* text, long long* out, char* suffix) {
    char* end;
    long long value = 1, multiplier = 1;
    static const char units[] = "KMGTPEZY";
    const char* p = text;

    if (suffix != NULL)
        suffix[0] = '\0';
    if (isdigit((unsigned char)*p))
        value = strtoll(p, &end, 10);
    else
        end = (char*)p;
    if (*end != '\0') {
        const char* unit = strchr(units, toupper((unsigned char)*end));
        int base = 1024, i;

        if (unit == NULL || *unit == '\0')
            return -1;
        if (end[1] == 'B' && end[2] == '\0')
            base = 1000;
        else if (!(end[1] == '\0' || (end[1] == 'i' && toupper((unsigned char)end[2]) == 'B' && end[3] == '\0')))
            return -1;
        for (i = 0; i <= (int)(unit - units); i++)
            multiplier *= base;
    }
    if (value <= 0)
        return -1;
    if (suffix != NULL && !isdigit((unsigned char)*text) && *end != '\0') {
        // -BK shows 10K, -BKB shows 10kB, -BKiB 10KiB
        snprintf(suffix, 8, "%c%s", (end[1] == 'B' && toupper((unsigned char)*end) == 'K') ? 'k' : toupper((unsigned char)*end), end + 1);
    }
    *out = value * multiplier;
    return 0;
}

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;

    switch (id) {
        case OPT_NULL:        s->terminator = 1; break;
        case OPT_ALL:         s->all = 1; break;
        case OPT_APPARENT:    s->apparent = 1; break;
        case OPT_BLOCK_SIZE:
            if (parse_block_size(value, &s->blockSize, s->blockSuffix) != 0) {
                fprintf(stderr, PROGRAM ": invalid --block-size argument '%s'\n", value);
                return -1;
            }
            s->human = 0;
            s->si = 0;
            break;
        case OPT_BYTES:       s->apparent = 1; s->blockSize = 1; s->human = 0; s->blockSuffix[0] = '\0'; break;
        case OPT_TOTAL:       s->total = 1; break;
        case OPT_MAX_DEPTH: {
            char* end;

            s->maxDepth = strtoll(value, &end, 10);
            if (*value == '\0' || *end != '\0' || s->maxDepth < 0) {
                fprintf(stderr, PROGRAM ": invalid maximum depth '%s'\n", value);
                return -1;
            }
            break;
        }
        case OPT_EXCLUDE:
            if (s->excludeCount < 16)
                s->excludes[s->excludeCount++] = value;
            break;
        case OPT_HUMAN:       s->human = 1; s->si = 0; break;
        case OPT_SI:          s->human = 1; s->si = 1; break;
        case OPT_K:           s->blockSize = 1024; s->human = 0; s->blockSuffix[0] = '\0'; break;
        case OPT_M:           s->blockSize = 1024 * 1024; s->human = 0; s->blockSuffix[0] = '\0'; break;
        case OPT_DEREFERENCE: s->dereference = 1; break;
        case OPT_PHYSICAL:    s->dereference = 0; break;
        case OPT_COUNT_LINKS: s->countLinks = 1; break;
        case OPT_SEPARATE:    s->separate = 1; break;
        case OPT_SUMMARIZE:   s->summarize = 1; break;
        case OPT_ONE_FS:      s->oneFileSystem = 1; break;
    }
    return 0;
}

/* ---- printing sizes -------------------------------------------------------------------------- */

static void print_size(const Settings* s, unsigned long long bytes) {
    if (s->human) {
        static const char suffixes[] = "KMGTPEZY";
        unsigned long long base = s->si ? 1000 : 1024;
        unsigned long long unit = 1;
        int power = 0;

        // the largest power that fits, with the value rounded up: below 10 with a decimal, else whole
        while (bytes / unit >= base && power < 8) {
            unit *= base;
            power++;
        }
        if (power == 0) {
            printf("%llu", bytes);
        } else {
            double value = (double)bytes / (double)unit;
            unsigned long long tenths = (unsigned long long)(value * 10.0);

            if ((double)tenths < value * 10.0)
                tenths++;
            if (tenths < 100) {
                printf("%llu.%llu%c", tenths / 10, tenths % 10, (s->si && power == 1) ? 'k' : suffixes[power - 1]);
            } else {
                unsigned long long whole = (unsigned long long)value;

                if ((double)whole < value)
                    whole++;
                if (whole >= base && power < 8) {
                    printf("1.0%c", suffixes[power]);
                } else {
                    printf("%llu%c", whole, (s->si && power == 1) ? 'k' : suffixes[power - 1]);
                }
            }
        }
    } else {
        unsigned long long blocks = (bytes + (unsigned long long)s->blockSize - 1) / (unsigned long long)s->blockSize;

        printf("%llu%s", blocks, s->blockSuffix);
    }
}

static void print_line(const Settings* s, unsigned long long bytes, const char* name) {
    print_size(s, bytes);
    putchar('\t');
    fputs(name, stdout);
    putchar(s->terminator ? '\0' : '\n');
}

/* ---- the walk -------------------------------------------------------------------------------- */

typedef struct {
    unsigned long volume;
    unsigned long long index;
} FileId;

typedef struct {
    const Settings* s;
    FileId*         seen;
    size_t          seenCount, seenCapacity;
    int             status;
    unsigned long   startVolume;
    int             remember;       /* several arguments: everything visited is noted, so that one inside another is not counted twice */
} Walk;

static int already_seen(Walk* w, unsigned long volume, unsigned long long index) {
    size_t i;

    for (i = 0; i < w->seenCount; i++)
        if (w->seen[i].volume == volume && w->seen[i].index == index)
            return 1;
    if (w->seenCount == w->seenCapacity) {
        size_t capacity = w->seenCapacity == 0 ? 64 : w->seenCapacity * 2;
        FileId* grown = realloc(w->seen, capacity * sizeof *grown);

        if (grown == NULL)
            return 0;
        w->seen = grown;
        w->seenCapacity = capacity;
    }
    w->seen[w->seenCount].volume = volume;
    w->seen[w->seenCount].index = index;
    w->seenCount++;
    return 0;
}

/** A simple wildcard match (* and ?) of the whole text. */
static int wildcard(const char* pattern, const char* text) {
    while (*pattern != '\0') {
        if (*pattern == '*') {
            while (*pattern == '*')
                pattern++;
            if (*pattern == '\0')
                return 1;
            for (; *text != '\0'; text++)
                if (wildcard(pattern, text))
                    return 1;
            return wildcard(pattern, text);
        }
        if (*text == '\0' || (*pattern != '?' && tolower((unsigned char)*pattern) != tolower((unsigned char)*text)))
            return 0;
        pattern++;
        text++;
    }
    return *text == '\0';
}

static int excluded(const Settings* s, const char* display) {
    char base[VPATH_MAX];
    int i;

    fs_basename(display, base, sizeof base);
    for (i = 0; i < s->excludeCount; i++)
        if (wildcard(s->excludes[i], base) || wildcard(s->excludes[i], display))
            return 1;
    return 0;
}

/**
 * The space of a file or of a directory with everything in it. depth is 0 for the argument. `display` is the name
 * printed; returns the sum in bytes. A directory's line is printed here, after its contents.
 */
static unsigned long long walk_path(Walk* w, const char* display, const char* native, int depth, int* isDirectory) {
    const Settings* s = w->s;
    HANDLE handle;
    BY_HANDLE_FILE_INFORMATION info;
    FILE_STANDARD_INFO standard;
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    unsigned long long own, total;
    int directory, isLink;

    if (!s->dereference)
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    handle = CreateFileA(native, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                         flags, NULL);
    if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &info)) {
        fprintf(stderr, PROGRAM ": cannot access '%s': %s\n", display, fs_strerror(GetLastError()));
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
        w->status = 1;
        *isDirectory = 0;
        return 0;
    }
    memset(&standard, 0, sizeof standard);
    GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof standard);
    CloseHandle(handle);

    directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    isLink = (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && !s->dereference;
    *isDirectory = directory && !isLink;

    // hard links: a file with several names is counted at its first
    if (((!directory && info.nNumberOfLinks > 1 && !s->countLinks) || w->remember) &&
        already_seen(w, info.dwVolumeSerialNumber, ((unsigned long long)info.nFileIndexHigh << 32) | info.nFileIndexLow))
        return 0;
    if (depth == 0)
        w->startVolume = info.dwVolumeSerialNumber;
    else if (s->oneFileSystem && info.dwVolumeSerialNumber != w->startVolume)
        return 0;

    own = s->apparent ? (((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow)
                      : (standard.AllocationSize.QuadPart > 0 ? (unsigned long long)standard.AllocationSize.QuadPart : 0);
    if (isLink && s->apparent)
        own = 0;
    total = own;

    if (directory && !isLink) {
        char pattern[VPATH_MAX * 2];
        WIN32_FIND_DATAA data;
        HANDLE find;
        unsigned long long subdirectories = 0;

        snprintf(pattern, sizeof pattern, "%s\\*", native);
        find = FindFirstFileA(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) {
            fprintf(stderr, PROGRAM ": cannot read directory '%s': %s\n", display, fs_strerror(GetLastError()));
            w->status = 1;
        } else {
            do {
                char childDisplay[VPATH_MAX * 2], childNative[VPATH_MAX * 2];
                unsigned long long size;
                int childDirectory;

                if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
                    continue;
                fs_join(display, data.cFileName, childDisplay, sizeof childDisplay);
                if (excluded(s, childDisplay))
                    continue;
                snprintf(childNative, sizeof childNative, "%s\\%s", native, data.cFileName);
                size = walk_path(w, childDisplay, childNative, depth + 1, &childDirectory);
                if (childDirectory)
                    subdirectories += size;
                total += size;
            } while (FindNextFileA(find, &data));
            FindClose(find);
        }
        if (s->separate)
            total -= subdirectories;
        if (!s->summarize || depth == 0) {
            if (s->maxDepth < 0 || depth <= s->maxDepth)
                print_line(s, total, display);
        }
        if (s->separate)
            total += subdirectories;       // the parent still counts what is below
    } else if (depth == 0 || (s->all && !s->summarize && (s->maxDepth < 0 || depth <= s->maxDepth))) {
        print_line(s, total, display);
    }
    return total;
}

int main(int argc, char* argv[]) {
    Settings s;
    const char** operands;
    int count, result, i;
    Walk walk;
    unsigned long long grand = 0;
    const char* defaultOperand[2] = { ".", NULL };

    _setmode(_fileno(stdout), _O_BINARY);
    memset(&s, 0, sizeof s);
    s.maxDepth = -1;
    s.blockSize = 1024;
    {
        const char* env = getenv("POSIXLY_CORRECT");

        if (env != NULL)
            s.blockSize = 512;
        env = getenv("DU_BLOCK_SIZE");
        if (env == NULL)
            env = getenv("BLOCK_SIZE");
        if (env == NULL)
            env = getenv("BLOCKSIZE");
        if (env != NULL && env[0] != '\0')
            parse_block_size(env, &s.blockSize, s.blockSuffix);
    }
    result = args_parse(&spec, argc, argv, on_option, &s, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (s.summarize && s.maxDepth >= 0) {
        if (s.maxDepth != 0) {
            fprintf(stderr, PROGRAM ": warning: summarizing conflicts with --max-depth=%lld\n", s.maxDepth);
            args_usage_error(&spec);
            return 1;
        }
    }
    if (s.summarize)
        s.maxDepth = 0;
    if (count == 0) {
        operands = defaultOperand;
        count = 1;
    }

    memset(&walk, 0, sizeof walk);
    walk.s = &s;
    walk.remember = count > 1;
    for (i = 0; i < count; i++) {
        char native[VPATH_MAX];
        int directory;

        if (vpath_to_native(operands[i], native, sizeof native) != 0) {
            fprintf(stderr, PROGRAM ": cannot access '%s': No such file or directory\n", operands[i]);
            walk.status = 1;
            continue;
        }
        if (excluded(&s, operands[i]))
            continue;
        grand += walk_path(&walk, operands[i], native, 0, &directory);
    }
    if (s.total)
        print_line(&s, grand, "total");
    free(walk.seen);
    return fflush(stdout) != 0 ? 1 : walk.status;
}
