/*
 * df: shows the space of the file systems, as GNU df does: of those that hold the FILEs, or of all the drives. A drive is
 * a file system ("C:" mounted on "/c" of the virtual root); sizes come from GetDiskFreeSpaceEx, the type from
 * GetVolumeInformation (ntfs, fat32, ...). Windows has no inodes: those columns show "-". The sizes are in blocks
 * rounded up (-k, -B, -P), or with -h / -H in powers of 1024 / 1000; --output picks the columns; --total adds a line.
 * --sync and --no-sync are accepted and do nothing.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "df"

typedef unsigned long long u64;

static void print_usage(void) {
    puts("Usage: df [OPTION]... [FILE]...\n"
         "Show information about the file system on which each FILE resides,\n"
         "or all file systems by default.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  -a, --all             include pseudo, duplicate, inaccessible file systems\n"
         "  -B, --block-size=SIZE  scale sizes by SIZE before printing them; e.g.,\n"
         "                           '-BM' prints sizes in units of 1,048,576 bytes;\n"
         "                           see SIZE format below\n"
         "  -h, --human-readable  print sizes in powers of 1024 (e.g., 1023M)\n"
         "  -H, --si              print sizes in powers of 1000 (e.g., 1.1G)\n"
         "  -i, --inodes          list inode information instead of block usage\n"
         "  -k                    like --block-size=1K\n"
         "  -l, --local           limit listing to local file systems\n"
         "      --no-sync         do not invoke sync before getting usage info (default)\n"
         "      --output[=FIELD_LIST]  use the output format defined by FIELD_LIST,\n"
         "                               or print all fields if FIELD_LIST is omitted.\n"
         "  -P, --portability     use the POSIX output format\n"
         "      --sync            invoke sync before getting usage info\n"
         "      --total           elide all entries insignificant to available space,\n"
         "                          and produce a grand total\n"
         "  -t, --type=TYPE       limit listing to file systems of type TYPE\n"
         "  -T, --print-type      print file system type\n"
         "  -x, --exclude-type=TYPE   limit listing to file systems not of type TYPE\n"
         "  -v                    (ignored)\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Display values are in units of the first available SIZE from --block-size,\n"
         "and the DF_BLOCK_SIZE, BLOCK_SIZE and BLOCKSIZE environment variables.\n"
         "Otherwise, units default to 1024 bytes (or 512 if POSIXLY_CORRECT is set).\n"
         "\n"
         "The SIZE argument is an integer and optional unit (example: 10K is 10*1024).\n"
         "Units are K,M,G,T,P,E,Z,Y (powers of 1024) or KB,MB,... (powers of 1000).\n"
         "Binary prefixes can be used, too: KiB=K, MiB=M, and so on.\n"
         "\n"
         "FIELD_LIST is a comma-separated list of columns to be included.  Valid\n"
         "field names are: 'source', 'fstype', 'itotal', 'iused', 'iavail', 'ipcent',\n"
         "'size', 'used', 'avail', 'pcent', 'file' and 'target' (see info page).");
}

enum {
    OPT_ALL = 1, OPT_BLOCK, OPT_HUMAN, OPT_SI, OPT_INODES, OPT_K, OPT_LOCAL, OPT_NOSYNC, OPT_OUTPUT, OPT_PORTABILITY, OPT_SYNC,
    OPT_TOTAL, OPT_TYPE, OPT_PRINT_TYPE, OPT_EXCLUDE, OPT_V
};

static const ArgsOption options[] = {
    { OPT_ALL,         'a', "all",             ARGS_NONE },
    { OPT_BLOCK,       'B', "block-size",      ARGS_REQUIRED },
    { OPT_HUMAN,       'h', "human-readable",  ARGS_NONE },
    { OPT_SI,          'H', "si",              ARGS_NONE },
    { OPT_INODES,      'i', "inodes",          ARGS_NONE },
    { OPT_K,           'k', NULL,              ARGS_NONE },
    { OPT_LOCAL,       'l', "local",           ARGS_NONE },
    { OPT_NOSYNC,      0,   "no-sync",         ARGS_NONE },
    { OPT_OUTPUT,      0,   "output",          ARGS_OPTIONAL },
    { OPT_PORTABILITY, 'P', "portability",     ARGS_NONE },
    { OPT_SYNC,        0,   "sync",            ARGS_NONE },
    { OPT_TOTAL,       0,   "total",           ARGS_NONE },
    { OPT_TYPE,        't', "type",            ARGS_REQUIRED },
    { OPT_PRINT_TYPE,  'T', "print-type",      ARGS_NONE },
    { OPT_EXCLUDE,     'x', "exclude-type",    ARGS_REQUIRED },
    { OPT_V,           'v', NULL,              ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

/* ---- the options ---- */

typedef enum { UNITS_BLOCKS, UNITS_HUMAN, UNITS_SI } Units;

static Units units = UNITS_BLOCKS;
static u64 blockSize;                   /* 0 until chosen */
static int showAll, showInodes, localOnly, portability, printType, showTotal, haveOutput;
static const char* outputList;
static const char* includeTypes[64];
static const char* excludeTypes[64];
static int includeCount, excludeCount;
static int blockOptionGiven;
static char suffixText[8];             /* -BK: the numbers get this suffix; empty with digits */

static const char* quote(const char* text) {
    static char buffer[1024];
    size_t n = 0;
    const unsigned char* p;

    buffer[n++] = '\'';
    for (p = (const unsigned char*)text; *p != '\0' && n < sizeof buffer - 8; p++) {
        if (*p == '\\' || *p == '\'')
            buffer[n++] = '\\';
        buffer[n++] = (char)*p;
    }
    buffer[n++] = '\'';
    buffer[n] = '\0';
    return buffer;
}

/** Parses a block size such as 1, 1K, 1KB, 1MiB, K. Returns 0, or -1 after writing the reason to message. */
static int parse_block_size(const char* text, const char* what, u64* out, char* message, size_t size) {
    const char* p = text;
    u64 value = 0, multiplier = 1;
    int any = 0, overflow = 0;
    const char* letters = "KMGTPEZY";

    for (; isdigit((unsigned char)*p); p++) {
        any = 1;
        if (value > (~0ULL - (u64)(*p - '0')) / 10)
            overflow = 1;
        value = value * 10 + (u64)(*p - '0');
    }
    suffixText[0] = '\0';
    if (*p != '\0' || !any) {
        int base = 1024;
        const char* at;
        int i;

        if (!any)
            value = 1;
        if (*p == '\0') {
            snprintf(message, size, "invalid %s argument %s", what, quote(text));
            return -1;
        }
        at = strchr(letters, *p == 'k' ? 'K' : *p);
        if (at == NULL || *p == '\0') {
            snprintf(message, size, any ? "invalid suffix in %s argument %s" : "invalid %s argument %s", what, quote(text));
            return -1;
        }
        if (p[1] == '\0' || (p[1] == 'i' && p[2] == 'B' && p[3] == '\0')) {
            base = 1024;
        } else if (p[1] == 'B' && p[2] == '\0') {
            base = 1000;
        } else {
            snprintf(message, size, any ? "invalid suffix in %s argument %s" : "invalid %s argument %s", what, quote(text));
            return -1;
        }
        if (!any) {
            char letter = *at;

            if (p[1] == 'B')
                snprintf(suffixText, sizeof suffixText, "%c%s", letter == 'K' ? 'k' : letter, "B");
            else if (p[1] == 'i')
                snprintf(suffixText, sizeof suffixText, "%ciB", letter);
            else
                snprintf(suffixText, sizeof suffixText, "%c", letter);
        }
        for (i = 0; i <= (int)(at - letters); i++) {
            if (multiplier > ~0ULL / (u64)base)
                overflow = 1;
            multiplier *= (u64)base;
        }
    }
    if (overflow || (multiplier != 0 && value > ~0ULL / multiplier)) {
        snprintf(message, size, "%s argument %s too large", what, quote(text));
        return -1;
    }
    value *= multiplier;
    if (value == 0) {
        snprintf(message, size, "invalid %s argument %s", what, quote(text));
        return -1;
    }
    *out = value;
    return 0;
}

static int on_option(int id, const char* value, void* context) {
    char message[2200];

    (void)context;
    switch (id) {
    case OPT_ALL:
        showAll = 1;
        break;
    case OPT_BLOCK:
        if (parse_block_size(value, "-B", &blockSize, message, sizeof message) != 0) {
            fprintf(stderr, PROGRAM ": %s\n", message);
            return -1;
        }
        units = UNITS_BLOCKS;
        blockOptionGiven = 1;
        break;
    case OPT_HUMAN:
        suffixText[0] = '\0';
        units = UNITS_HUMAN;
        blockOptionGiven = 1;
        break;
    case OPT_SI:
        suffixText[0] = '\0';
        units = UNITS_SI;
        blockOptionGiven = 1;
        break;
    case OPT_K:
        suffixText[0] = '\0';
        units = UNITS_BLOCKS;
        blockSize = 1024;
        blockOptionGiven = 1;
        break;
    case OPT_INODES:
        showInodes = 1;
        break;
    case OPT_LOCAL:
        localOnly = 1;
        break;
    case OPT_OUTPUT:
        haveOutput = 1;
        outputList = value;
        break;
    case OPT_PORTABILITY:
        portability = 1;
        break;
    case OPT_TOTAL:
        showTotal = 1;
        break;
    case OPT_TYPE:
        if (includeCount < 64)
            includeTypes[includeCount++] = value;
        break;
    case OPT_EXCLUDE:
        if (excludeCount < 64)
            excludeTypes[excludeCount++] = value;
        break;
    case OPT_PRINT_TYPE:
        printType = 1;
        break;
    default:
        break;
    }
    return 0;
}

/* ---- the file systems ---- */

typedef struct {
    char source[MAX_PATH * 2];
    char target[MAX_PATH * 2];
    char file[MAX_PATH * 2];
    char type[64];
    int  known;
    u64  size, used, avail;
    int  local;
} Entry;

static Entry* entries;
static size_t entryCount, entryCapacity;

static Entry* add_entry(void) {
    if (entryCount == entryCapacity) {
        entryCapacity = entryCapacity == 0 ? 16 : entryCapacity * 2;
        entries = realloc(entries, entryCapacity * sizeof *entries);
        if (entries == NULL) {
            fprintf(stderr, PROGRAM ": memory exhausted\n");
            exit(1);
        }
    }
    memset(&entries[entryCount], 0, sizeof entries[entryCount]);
    return &entries[entryCount++];
}

static void lower(char* s) {
    for (; *s != '\0'; s++)
        *s = (char)tolower((unsigned char)*s);
}

/** Fills the sizes and the type of a root such as "C:\". */
static void read_volume(Entry* e, const char* root) {
    ULARGE_INTEGER avail, total, freeBytes;
    char type[64];

    e->known = 0;
    snprintf(e->type, sizeof e->type, "-");
    if (GetDiskFreeSpaceExA(root, &avail, &total, &freeBytes)) {
        e->known = 1;
        e->size = total.QuadPart;
        e->avail = avail.QuadPart;
        e->used = total.QuadPart - freeBytes.QuadPart;
    }
    if (GetVolumeInformationA(root, NULL, 0, NULL, NULL, NULL, type, sizeof type) && type[0] != '\0') {
        snprintf(e->type, sizeof e->type, "%s", type);
        lower(e->type);
    }
}

static void add_drive(char letter, UINT driveType) {
    Entry* e = add_entry();
    char root[4];

    root[0] = letter;
    root[1] = ':';
    root[2] = '\\';
    root[3] = '\0';
    snprintf(e->source, sizeof e->source, "%c:", letter);
    snprintf(e->target, sizeof e->target, "/%c", tolower((unsigned char)letter));
    snprintf(e->file, sizeof e->file, "-");
    e->local = driveType != DRIVE_REMOTE;
    read_volume(e, root);
}

static void list_all(void) {
    DWORD mask = GetLogicalDrives();
    char letter;

    for (letter = 'A'; letter <= 'Z'; letter++) {
        UINT type;
        char root[4];

        if (!(mask & (1u << (letter - 'A'))))
            continue;
        root[0] = letter;
        root[1] = ':';
        root[2] = '\\';
        root[3] = '\0';
        type = GetDriveTypeA(root);
        if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_UNKNOWN)
            continue;
        add_drive(letter, type);
        if (!entries[entryCount - 1].known && !showAll)
            entryCount--;               // not ready (an empty drive): only with -a
    }
}

static int add_file(const char* name) {
    char native[VPATH_MAX], full[VPATH_MAX];
    DWORD attributes;
    Entry* e;

    if (vpath_to_native(name, native, sizeof native) != 0 || fs_attributes(native, &attributes) != 0) {
        fprintf(stderr, PROGRAM ": %s: No such file or directory\n", name);
        return 0;
    }
    if (GetFullPathNameA(native, sizeof full, full, NULL) == 0) {
        fprintf(stderr, PROGRAM ": %s: No such file or directory\n", name);
        return 0;
    }
    if (isalpha((unsigned char)full[0]) && full[1] == ':') {
        char root[4];

        root[0] = full[0];
        root[1] = ':';
        root[2] = '\\';
        root[3] = '\0';
        e = add_entry();
        snprintf(e->source, sizeof e->source, "%c:", toupper((unsigned char)full[0]));
        snprintf(e->target, sizeof e->target, "/%c", tolower((unsigned char)full[0]));
        e->local = GetDriveTypeA(root) != DRIVE_REMOTE;
        read_volume(e, root);
    } else if (full[0] == '\\' && full[1] == '\\') {
        char root[MAX_PATH * 2];
        char* p = full + 2;
        int slashes = 0;
        size_t n;

        while (*p != '\0' && slashes < 2) {
            if (*p == '\\')
                slashes++;
            if (slashes < 2)
                p++;
        }
        n = (size_t)(p - full);
        snprintf(root, sizeof root, "%.*s\\", (int)n, full);
        e = add_entry();
        snprintf(e->source, sizeof e->source, "%.*s", (int)n, full);
        snprintf(e->target, sizeof e->target, "%.*s", (int)n, full);
        e->local = 0;
        read_volume(e, root);
    } else {
        fprintf(stderr, PROGRAM ": %s: No such file or directory\n", name);
        return 0;
    }
    snprintf(e->file, sizeof e->file, "%s", name);
    return 1;
}

static int type_selected(const Entry* e) {
    int i, ok = includeCount == 0;

    for (i = 0; i < includeCount; i++) {
        if (strcmp(includeTypes[i], e->type) == 0)
            ok = 1;
    }
    for (i = 0; i < excludeCount; i++) {
        if (strcmp(excludeTypes[i], e->type) == 0)
            ok = 0;
    }
    return ok;
}

/* ---- the numbers ---- */

/** A size as the units say: blocks rounded up, or with the suffix of powers of 1024 / 1000. */
static void format_size(u64 bytes, char* out, size_t size) {
    if (units == UNITS_BLOCKS) {
        u64 q = bytes / blockSize + (bytes % blockSize != 0);

        snprintf(out, size, "%llu%s", q, suffixText);
        return;
    }
    {
        unsigned int base = units == UNITS_HUMAN ? 1024 : 1000;
        const char* suffixes = units == UNITS_HUMAN ? "KMGTPEZY" : "kMGTPEZY";
        u64 divisor = 1;
        int power = 0;

        while (bytes / divisor >= base && power < 8) {
            divisor *= base;
            power++;
            if (divisor > ~0ULL / base)
                break;
        }
        for (;;) {
            u64 q = bytes / divisor, r = bytes % divisor;

            if (power == 0) {
                snprintf(out, size, "%llu", bytes);
                return;
            }
            if (q < 10) {
                u64 tenths = q * 10 + (r * 10 + divisor - 1) / divisor;

                if (tenths >= 100) {
                    snprintf(out, size, "%llu%c", tenths / 10, suffixes[power - 1]);
                    return;
                }
                snprintf(out, size, "%llu.%llu%c", tenths / 10, tenths % 10, suffixes[power - 1]);
                return;
            }
            q += r != 0;
            if (q >= base && power < 8) {
                divisor *= base;
                power++;
                continue;
            }
            snprintf(out, size, "%llu%c", q, suffixes[power - 1]);
            return;
        }
    }
}

static void format_percent(u64 used, u64 avail, char* out, size_t size) {
    u64 total = used + avail;

    if (total == 0) {
        snprintf(out, size, "-");
        return;
    }
    {
        u64 pct;

        if (used < (1ULL << 56))
            pct = (used * 100 + total - 1) / total;
        else
            pct = (u64)((double)used * 100.0 / (double)total + 0.999999);
        snprintf(out, size, "%llu%%", pct);
    }
}

/** The text of the block size for the header: 1K, 1M, 1kB, 1.5MB, 512B. */
static void block_header(char* out, size_t size) {
    const char* iec = "KMGTPEZY";
    u64 n = blockSize;
    int power = 0;

    if (blockSize == 1) {
        snprintf(out, size, "1B");
        return;
    }
    while (n % 1024 == 0 && power < 8) {
        n /= 1024;
        power++;
    }
    if (power > 0) {
        snprintf(out, size, "%llu%c", n, iec[power - 1]);
        return;
    }
    {
        // not a multiple of 1024: powers of 1000, rounded up, with the B
        const char* si = "kMGTPEZY";
        u64 divisor = 1;
        int p = 0;
        u64 q, r;

        while (blockSize / divisor >= 1000 && p < 8) {
            divisor *= 1000;
            p++;
        }
        q = blockSize / divisor;
        r = blockSize % divisor;
        if (p == 0) {
            snprintf(out, size, "%lluB", blockSize);
        } else if (q < 10) {
            u64 tenths = q * 10 + (r * 10 + divisor - 1) / divisor;

            if (tenths % 10 == 0)
                snprintf(out, size, "%llu%cB", tenths / 10, si[p - 1]);
            else
                snprintf(out, size, "%llu.%llu%cB", tenths / 10, tenths % 10, si[p - 1]);
        } else {
            snprintf(out, size, "%llu%cB", q + (r != 0), si[p - 1]);
        }
    }
}

/* ---- the table ---- */

typedef enum {
    F_SOURCE, F_FSTYPE, F_ITOTAL, F_IUSED, F_IAVAIL, F_IPCENT, F_SIZE, F_USED, F_AVAIL, F_PCENT, F_FILE, F_TARGET, F_COUNT
} Field;

static const char* const fieldNames[F_COUNT] = {
    "source", "fstype", "itotal", "iused", "iavail", "ipcent", "size", "used", "avail", "pcent", "file", "target"
};

static Field columns[F_COUNT * 2];
static int columnCount;

static void choose_columns(void) {
    int i;

    if (haveOutput) {
        if (outputList == NULL) {
            for (i = 0; i < F_COUNT; i++)
                columns[columnCount++] = (Field)i;
        } else {
            const char* p = outputList;
            int seen[F_COUNT] = { 0 };

            for (;;) {
                const char* comma = strchr(p, ',');
                size_t length = comma != NULL ? (size_t)(comma - p) : strlen(p);
                int found = -1;

                for (i = 0; i < F_COUNT; i++) {
                    if (strlen(fieldNames[i]) == length && strncmp(fieldNames[i], p, length) == 0)
                        found = i;
                }
                if (found < 0) {
                    char name[256];

                    snprintf(name, sizeof name, "%.*s", (int)length, p);
                    fprintf(stderr, PROGRAM ": option --output: field %s unknown\n", quote(name));
                    args_usage_error(&spec);
                    exit(1);
                }
                if (seen[found]) {
                    fprintf(stderr, PROGRAM ": option --output: field %s used more than once\n", quote(fieldNames[found]));
                    args_usage_error(&spec);
                    exit(1);
                }
                seen[found] = 1;
                columns[columnCount++] = (Field)found;
                if (comma == NULL)
                    break;
                p = comma + 1;
            }
        }
        return;
    }
    columns[columnCount++] = F_SOURCE;
    if (printType)
        columns[columnCount++] = F_FSTYPE;
    if (showInodes) {
        columns[columnCount++] = F_ITOTAL;
        columns[columnCount++] = F_IUSED;
        columns[columnCount++] = F_IAVAIL;
        columns[columnCount++] = F_IPCENT;
    } else {
        columns[columnCount++] = F_SIZE;
        columns[columnCount++] = F_USED;
        columns[columnCount++] = F_AVAIL;
        columns[columnCount++] = F_PCENT;
    }
    columns[columnCount++] = F_TARGET;
}

static const char* header_text(Field f, char* buffer, size_t size) {
    switch (f) {
    case F_SOURCE: return "Filesystem";
    case F_FSTYPE: return "Type";
    case F_ITOTAL: return "Inodes";
    case F_IUSED: return "IUsed";
    case F_IAVAIL: return "IFree";
    case F_IPCENT: return "IUse%";
    case F_SIZE:
        if (units != UNITS_BLOCKS)
            return "Size";
        {
            char h[64];

            block_header(h, sizeof h);
            if (portability && !haveOutput)
                snprintf(buffer, size, "%llu-blocks", blockSize);
            else if (suffixText[0] != '\0')
                snprintf(buffer, size, "1%s-blocks", suffixText);
            else
                snprintf(buffer, size, "%s-blocks", h);
        }
        return buffer;
    case F_USED: return "Used";
    case F_AVAIL: return (haveOutput || units != UNITS_BLOCKS) ? "Avail" : "Available";
    case F_PCENT: return (portability && !haveOutput && units == UNITS_BLOCKS) ? "Capacity" : "Use%";
    case F_FILE: return "File";
    default: return "Mounted on";
    }
}

static int left_aligned(Field f) {
    return f == F_SOURCE || f == F_FSTYPE || f == F_FILE || f == F_TARGET;
}

static void cell_text(const Entry* e, int isTotal, Field f, char* out, size_t size) {
    switch (f) {
    case F_SOURCE: snprintf(out, size, "%s", isTotal ? "total" : e->source); break;
    case F_FSTYPE: snprintf(out, size, "%s", isTotal ? "-" : e->type); break;
    case F_ITOTAL:
    case F_IUSED:
    case F_IAVAIL:
        snprintf(out, size, "%s", isTotal ? "0" : "-");
        break;
    case F_IPCENT: snprintf(out, size, "-"); break;
    case F_SIZE:
        if (!e->known && !isTotal)
            snprintf(out, size, "-");
        else
            format_size(e->size, out, size);
        break;
    case F_USED:
        if (!e->known && !isTotal)
            snprintf(out, size, "-");
        else
            format_size(e->used, out, size);
        break;
    case F_AVAIL:
        if (!e->known && !isTotal)
            snprintf(out, size, "-");
        else
            format_size(e->avail, out, size);
        break;
    case F_PCENT:
        if (!e->known && !isTotal)
            snprintf(out, size, "-");
        else
            format_percent(e->used, e->avail, out, size);
        break;
    case F_FILE: snprintf(out, size, "%s", isTotal ? "-" : e->file); break;
    default: snprintf(out, size, "%s", isTotal ? "-" : e->target); break;
    }
}

static size_t text_width(const char* s) {
    // characters, not bytes (UTF-8)
    size_t n = 0;

    for (; *s != '\0'; s++) {
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    }
    return n;
}

static void print_table(const Entry* rows, size_t count, const Entry* total) {
    char** cells;
    size_t rowCount = count + 1 + (total != NULL ? 1 : 0), r;
    int c;
    size_t widths[F_COUNT * 2];
    char head[128];

    cells = calloc(rowCount * (size_t)columnCount, sizeof *cells);
    for (c = 0; c < columnCount; c++) {
        char buffer[160];
        const char* h = header_text(columns[c], head, sizeof head);

        snprintf(buffer, sizeof buffer, "%s", h);
        cells[c] = _strdup(buffer);
    }
    for (r = 0; r < count; r++) {
        for (c = 0; c < columnCount; c++) {
            char buffer[MAX_PATH * 2 + 64];

            cell_text(&rows[r], 0, columns[c], buffer, sizeof buffer);
            cells[(r + 1) * (size_t)columnCount + (size_t)c] = _strdup(buffer);
        }
    }
    if (total != NULL) {
        for (c = 0; c < columnCount; c++) {
            char buffer[160];

            cell_text(total, 1, columns[c], buffer, sizeof buffer);
            cells[(count + 1) * (size_t)columnCount + (size_t)c] = _strdup(buffer);
        }
    }
    for (c = 0; c < columnCount; c++) {
        size_t w = 0;

        switch (columns[c]) {
        case F_SOURCE: w = 14; break;
        case F_FSTYPE: w = 4; break;
        case F_PCENT: case F_IPCENT: w = 4; break;
        case F_ITOTAL: case F_IUSED: case F_IAVAIL: case F_SIZE: case F_USED: case F_AVAIL: w = 5; break;
        default: w = 1; break;
        }
        if (columns[c] == F_SOURCE && haveOutput)
            w = 14;
        for (r = 0; r < rowCount; r++) {
            size_t n = text_width(cells[r * (size_t)columnCount + (size_t)c]);

            if (n > w)
                w = n;
        }
        widths[c] = w;
    }
    for (r = 0; r < rowCount; r++) {
        for (c = 0; c < columnCount; c++) {
            const char* text = cells[r * (size_t)columnCount + (size_t)c];
            size_t pad = widths[c] - text_width(text);
            int last = c == columnCount - 1;

            if (c > 0)
                putchar(' ');
            if (left_aligned(columns[c])) {
                fputs(text, stdout);
                if (!last) {
                    while (pad-- > 0)
                        putchar(' ');
                }
            } else {
                while (pad-- > 0)
                    putchar(' ');
                fputs(text, stdout);
            }
            free(cells[r * (size_t)columnCount + (size_t)c]);
        }
        putchar('\n');
    }
    free(cells);
}

static int conflict(const char* a) {
    fprintf(stderr, PROGRAM ": options %s and --output are mutually exclusive\n", a);
    args_usage_error(&spec);
    return 1;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, i, ok = 1;
    size_t kept = 0, r;
    Entry* chosen;
    Entry total;

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (haveOutput) {
        if (showInodes)
            return conflict("-i");
        if (printType)
            return conflict("-T");
        if (portability)
            return conflict("-P");
    }
    if (!blockOptionGiven) {
        const char* names[] = { "DF_BLOCK_SIZE", "BLOCK_SIZE", "BLOCKSIZE" };
        int k;

        for (k = 0; k < 3; k++) {
            const char* v = getenv(names[k]);

            if (v != NULL && v[0] != '\0') {
                char message[2200];

                if (strcmp(v, "human-readable") == 0) {
                    units = UNITS_HUMAN;
                } else if (strcmp(v, "si") == 0) {
                    units = UNITS_SI;
                } else {
                    if (v[0] == '\'')
                        v++;
                    if (parse_block_size(v, "block size", &blockSize, message, sizeof message) != 0)
                        blockSize = 0;
                }
                break;
            }
        }
    }
    if (units == UNITS_BLOCKS && blockSize == 0)
        blockSize = getenv("POSIXLY_CORRECT") != NULL ? 512 : 1024;
    if (portability && units == UNITS_BLOCKS && !blockOptionGiven && getenv("POSIXLY_CORRECT") == NULL && blockSize == 1024)
        blockSize = 1024;
    {
        int a, b;

        for (a = 0; a < includeCount; a++) {
            for (b = 0; b < excludeCount; b++) {
                if (strcmp(includeTypes[a], excludeTypes[b]) == 0) {
                    fprintf(stderr, PROGRAM ": file system type %s both selected and excluded\n", quote(includeTypes[a]));
                    return 1;
                }
            }
        }
    }
    choose_columns();
    if (count == 0) {
        list_all();
    } else {
        for (i = 0; i < count; i++)
            ok &= add_file(operands[i]);
    }
    chosen = malloc((entryCount + 1) * sizeof *chosen);
    for (r = 0; r < entryCount; r++) {
        if (!type_selected(&entries[r]))
            continue;
        if (localOnly && !entries[r].local)
            continue;
        chosen[kept++] = entries[r];
    }
    if (kept == 0) {
        if (count == 0 || ok) {
            fprintf(stderr, PROGRAM ": no file systems processed\n");
            return 1;
        }
        return 1;
    }
    memset(&total, 0, sizeof total);
    total.known = 1;
    for (r = 0; r < kept; r++) {
        if (chosen[r].known) {
            total.size += chosen[r].size;
            total.used += chosen[r].used;
            total.avail += chosen[r].avail;
        }
    }
    print_table(chosen, kept, showTotal ? &total : NULL);
    return ok ? 0 : 1;
}
