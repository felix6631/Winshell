#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <io.h>
#include <fcntl.h>
#include <time.h>
#include <windows.h>
#include <aclapi.h>
#include "vpath.h"
#include "args.h"
#include "vec.h"
#include "fsutil.h"
#include "datetime.h"
#include "globmatch.h"
#include "utf8.h"

#define VERSION "0.1"
/* dir and vdir are this program with another name and other defaults (dir: -C -b, vdir: -l -b) */
#ifndef PROGRAM
#define PROGRAM "ls"
#endif
#ifndef LS_DEFAULT_FORMAT
#define LS_DEFAULT_FORMAT 0          /* 0 as ls, 1 columns, 2 long */
#endif
#ifndef LS_DEFAULT_ESCAPE
#define LS_DEFAULT_ESCAPE 0
#endif

/*
 * ls lists files and directories.
 *
 * Paths are those of the virtual file system, so "ls /" lists the drives.
 * Windows adaptations, in the spirit of doing the same job as on Linux:
 *  - A name is hidden when it starts with a dot (as on Linux) or the file
 *    has the hidden or system attribute (as on Windows).
 *  - Sorting ignores case, as Windows file names do.
 *  - The mode of -l is made up from the attributes: every entry is readable,
 *    it is writable unless read-only, and executable when it is a directory
 *    or a program (.exe .com .bat .cmd). "l" marks symbolic links and junctions.
 *  - Owner and group are the ones of the security descriptor of the file; with -n
 *    they are the last part of the SID (the RID), which is the number Cygwin and MSYS show.
 *  - The inode number is the file index of the volume, the number of links is the real one, and the blocks are
 *    the space the file takes (in blocks of 1024 bytes, as ls counts them unless told otherwise).
 *  - ctime is the time of the last change of the file record, birth the time of creation. A name made of
 *    characters that are not UTF-8 is written as bytes (\303).
 *  - Colours come from LS_COLORS (the built-in ones are those of GNU ls: directories, links and programs).
 *  - Output has LF line endings.
 */

typedef enum { FMT_SINGLE, FMT_COLUMNS, FMT_ACROSS, FMT_COMMAS, FMT_LONG } Format;
typedef enum { SORT_NAME, SORT_SIZE, SORT_TIME, SORT_VERSION, SORT_EXT, SORT_NONE } SortKey;
typedef enum { TIME_MTIME, TIME_ATIME, TIME_CTIME, TIME_BIRTH } TimeKind;
typedef enum { IND_NONE, IND_SLASH, IND_FILETYPE, IND_CLASSIFY } IndicatorStyle;
typedef enum {
    Q_LITERAL, Q_SHELL, Q_SHELL_ALWAYS, Q_SHELL_ESCAPE, Q_SHELL_ESCAPE_ALWAYS,
    Q_C, Q_C_MAYBE, Q_ESCAPE, Q_LOCALE, Q_CLOCALE
} Quoting;
typedef enum { COLOR_NEVER, COLOR_ALWAYS, COLOR_AUTO } ColorMode;

/** How numbers of bytes are written: plain, in units of a block size, or in the 1.5K way. */
typedef struct {
    int                human;           // -h and --si: scale by 1024 or 1000
    int                base;            // 1024 or 1000 for human
    unsigned long long block;           // the unit when not human; 1 writes the bytes
    char               suffix[8];       // written after a number in units of a block size: "K", "kB", "KiB"
} Units;

typedef struct {
    int            all;             // -a  also show hidden entries, . and ..
    int            almostAll;       // -A  also show hidden entries, but not . and ..
    Format         format;
    int            formatSet;
    int            directory;       // -d  list directories themselves
    int            recursive;       // -R
    int            reverse;         // -r
    SortKey        sort;
    int            sortSet;
    int            groupDirectories;
    int            deref;           // -L
    int            derefArgs;       // -H
    IndicatorStyle indicator;
    Quoting        quoting;
    int            quotingSet;
    int            hideControl;     // -q
    int            hideControlSet;
    int            context;         // -Z  the security context, which is always "?" here
    char*          timeStyleText;   // --time-style, not yet checked (only a long listing needs it)
    int            inode;           // -i
    int            blocks;          // -s
    int            owner, group, author;   // columns of the long format
    int            numeric;         // -n
    TimeKind       timeKind;
    char*          timeStyle;       // NULL for the default
    Units          fileUnits;       // the sizes of -l
    Units          blockUnits;      // the blocks of -s and "total"
    ColorMode      color;
    long           width;           // -w; -1 when not given
    long           tabSize;
    int            tabSet;          // -T was given
    char**         ignore;          // -I and -B, which also hide what -a shows
    char**         hide;            // --hide
    size_t         ignoreCount, hideCount;
} Options;

static Options opt;
static int stdoutIsConsole;
static int argumentError;

typedef struct {
    char*              name;        // as shown
    char*              native;      // Windows path; NULL for the entries of the virtual root with no file
    DWORD              attrs;
    unsigned long long size;
    FILETIME           mtime, atime, ctime, btime;
    unsigned long long alloc;       // bytes the file takes
    DWORD              nlink;
    unsigned long long inode;
    int                detailed;
    int                isLink;
    char*              linkText;    // target of a link, in the virtual file system
    int                linkOk;      // the target exists
    DWORD              linkAttrs;
} Entry;

typedef struct {
    Entry* items;
    size_t count, capacity;
} EntryList;

/** What a listing carries from one directory to the next. */
typedef struct {
    int firstBlock;                 // nothing was written yet: no blank line before the next block
    int exitStatus;
} Listing;

static void print_usage(void) {
    puts("Usage: " PROGRAM " [OPTION]... [FILE]...\n"
         "List information about the FILEs (the current directory by default).\n"
         "Sort entries alphabetically if none of -cftuvSUX nor --sort is specified.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  -a, --all                  do not ignore entries starting with .\n"
         "  -A, --almost-all           do not list implied . and ..\n"
         "      --author               with -l, print the author of each file\n"
         "  -b, --escape               print C-style escapes for nongraphic characters\n"
         "      --block-size=SIZE      with -l, scale sizes by SIZE when printing them;\n"
         "                               e.g., '--block-size=M'; see SIZE format below\n"
         "  -B, --ignore-backups       do not list implied entries ending with ~\n"
         "  -c                         with -lt: sort by, and show, ctime; with -l: show ctime and\n"
         "                               sort by name; otherwise: sort by ctime, newest first\n"
         "  -C                         list entries by columns\n"
         "      --color[=WHEN]         color the output WHEN; more info below\n"
         "  -d, --directory            list directories themselves, not their contents\n"
         "  -f                         do not sort, enable -aU, disable -ls --color\n"
         "  -F, --classify             append indicator (one of */=>@|) to entries\n"
         "      --file-type            likewise, except do not append '*'\n"
         "      --format=WORD          across -x, commas -m, horizontal -x, long -l,\n"
         "                               single-column -1, verbose -l, vertical -C\n"
         "      --full-time            like -l --time-style=full-iso\n"
         "  -g                         like -l, but do not list owner\n"
         "      --group-directories-first\n"
         "                             group directories before files\n"
         "  -G, --no-group             in a long listing, don't print group names\n"
         "  -h, --human-readable       with -l and -s, print sizes like 1K 234M 2G etc.\n"
         "      --si                   likewise, but use powers of 1000 not 1024\n"
         "  -H, --dereference-command-line\n"
         "                             follow symbolic links listed on the command line\n"
         "      --hide=PATTERN         do not list implied entries matching shell PATTERN\n"
         "                               (overridden by -a or -A)\n"
         "      --indicator-style=WORD  append indicator with style WORD to entry names:\n"
         "                               none (default), slash (-p), file-type, classify (-F)\n"
         "  -i, --inode                print the index number of each file\n"
         "  -I, --ignore=PATTERN       do not list implied entries matching shell PATTERN\n"
         "  -k, --kibibytes            default to 1024-byte blocks for file system usage\n"
         "  -l                         use a long listing format\n"
         "  -L, --dereference          when showing file information for a symbolic link, show\n"
         "                               information for the file the link references\n"
         "  -m                         fill width with a comma separated list of entries\n"
         "  -n, --numeric-uid-gid      like -l, but list numeric user and group IDs\n"
         "  -N, --literal              print entry names without quoting\n"
         "  -o                         like -l, but do not list group information\n"
         "  -p, --indicator-style=slash\n"
         "                             append / indicator to directories\n"
         "  -q, --hide-control-chars   print ? instead of nongraphic characters\n"
         "      --show-control-chars   show nongraphic characters as-is\n"
         "  -Q, --quote-name           enclose entry names in double quotes\n"
         "      --quoting-style=WORD   use quoting style WORD for entry names:\n"
         "                               literal, locale, shell, shell-always,\n"
         "                               shell-escape, shell-escape-always, c, escape\n"
         "  -r, --reverse              reverse order while sorting\n"
         "  -R, --recursive            list subdirectories recursively\n"
         "  -s, --size                 print the allocated size of each file, in blocks\n"
         "  -S                         sort by file size, largest first\n"
         "      --sort=WORD            sort by WORD instead of name: none (-U), size (-S),\n"
         "                               time (-t), version (-v), extension (-X)\n"
         "      --time=WORD            change the default of using modification times;\n"
         "                               access time (-u): atime, access, use;\n"
         "                               change time (-c): ctime, status;\n"
         "                               birth time: birth, creation;\n"
         "                             with -l, WORD determines which time to show;\n"
         "                             with --sort=time, sort by WORD (newest first)\n"
         "      --time-style=TIME_STYLE  time/date format with -l; see TIME_STYLE below\n"
         "  -t                         sort by time, newest first; see --time\n"
         "  -T, --tabsize=COLS         assume tab stops at each COLS instead of 8\n"
         "  -u                         with -lt: sort by, and show, access time;\n"
         "                               with -l: show access time and sort by name;\n"
         "                               otherwise: sort by access time, newest first\n"
         "  -U                         do not sort; list entries in directory order\n"
         "  -v                         natural sort of (version) numbers within text\n"
         "  -w, --width=COLS           set output width to COLS.  0 means no limit\n"
         "  -x                         list entries by lines instead of by columns\n"
         "  -X                         sort alphabetically by entry extension\n"
         "  -1                         list one file per line\n"
         "      --help                 display this help and exit\n"
         "      --version              output version information and exit\n"
         "\n"
         "The SIZE argument is an integer and optional unit (example: 10K is 10*1024).\n"
         "Units are K,M,G,T,P,E (powers of 1024) or KB,MB,... (powers of 1000).\n"
         "\n"
         "The TIME_STYLE argument can be full-iso, long-iso, iso, locale, or +FORMAT.\n"
         "FORMAT is interpreted like in date; if FORMAT is FORMAT1<newline>FORMAT2,\n"
         "then FORMAT1 applies to non-recent files and FORMAT2 to recent files.\n"
         "\n"
         "Using color to distinguish file types is disabled both by default and\n"
         "with --color=never.  With --color=auto, ls emits color codes only when\n"
         "standard output is connected to a console.  The LS_COLORS environment\n"
         "variable can change the settings; use the dircolors command to set it.\n"
         "\n"
         "The root directory / holds the drives: ls / shows c, d, ...\n"
         "Names starting with a dot, and files with the hidden or system attribute, are hidden.\n"
         "\n"
         "Exit status:\n"
         " 0  if OK,\n"
         " 1  if minor problems (e.g., cannot access subdirectory),\n"
         " 2  if serious trouble (e.g., cannot access command-line argument).");
}

/* ---- text buffers --------------------------------------------------------------- */

typedef struct {
    char*  data;
    size_t length, capacity;
} Buf;

static void out_of_memory(void) {
    fprintf(stderr, PROGRAM ": out of memory\n");
    exit(2);
}

static void buf_add(Buf* b, const char* text, size_t length) {
    if (VEC_RESERVE(b->data, b->capacity, b->length + length + 1) != 0)
        out_of_memory();
    memcpy(b->data + b->length, text, length);
    b->length += length;
    b->data[b->length] = '\0';
}

static void buf_str(Buf* b, const char* text) {
    buf_add(b, text, strlen(text));
}

static void buf_char(Buf* b, char c) {
    buf_add(b, &c, 1);
}

static void buf_free(Buf* b) {
    free(b->data);
    b->data = NULL;
    b->length = b->capacity = 0;
}

static char* xstrdup(const char* text) {
    char* copy = _strdup(text);
    if (copy == NULL)
        out_of_memory();
    return copy;
}

/* ---- entries -------------------------------------------------------------- */

static FILETIME zero_time(void) {
    FILETIME t;
    t.dwLowDateTime = t.dwHighDateTime = 0;
    return t;
}

static unsigned long long file_time_value(FILETIME t) {
    return ((unsigned long long)t.dwHighDateTime << 32) | t.dwLowDateTime;
}

static void entry_init(Entry* e, const char* name, const char* native) {
    memset(e, 0, sizeof *e);
    e->name = xstrdup(name);
    e->native = native != NULL ? xstrdup(native) : NULL;
    e->mtime = e->atime = e->ctime = e->btime = zero_time();
    e->nlink = 1;
}

static void entry_free(Entry* e) {
    free(e->name);
    free(e->native);
    free(e->linkText);
}

static void list_push(EntryList* list, const Entry* e) {
    if (VEC_RESERVE(list->items, list->capacity, list->count + 1) != 0)
        out_of_memory();
    list->items[list->count++] = *e;
}

/** Adds an entry that is known by its attributes only (and by its native path, for the details). */
static void list_add(EntryList* list, const char* name, const char* native, DWORD attrs,
                     unsigned long long size, FILETIME mtime) {
    Entry e;

    entry_init(&e, name, native);
    e.attrs = attrs;
    e.size = size;
    e.mtime = e.ctime = e.atime = e.btime = mtime;
    e.isLink = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    list_push(list, &e);
}

static void list_free(EntryList* list) {
    size_t i;
    for (i = 0; i < list->count; i++)
        entry_free(&list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = list->capacity = 0;
}

static int is_directory(const Entry* e) {
    return (e->attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/** Symbolic links and junctions. They are followed only with -L (and -H, for the names of the command line). */
static int is_link(const Entry* e) {
    return e->isLink;
}

static int has_extension(const char* name, const char* extension) {
    size_t n = strlen(name), e = strlen(extension);
    return n > e && _stricmp(name + n - e, extension) == 0;
}

static int name_is_program(const char* name) {
    return has_extension(name, ".exe") || has_extension(name, ".com") ||
           has_extension(name, ".bat") || has_extension(name, ".cmd");
}

static int is_program(const Entry* e) {
    return !is_directory(e) && name_is_program(e->name);
}

static int needs_details(void) {
    return opt.format == FMT_LONG || opt.inode || opt.blocks || opt.deref ||
           opt.color != COLOR_NEVER || opt.timeKind == TIME_CTIME;
}

/**
 * Asks the file system for what the find data lacks: links, inode, space, the times. With follow a link is
 * the file it points to (a link that points nowhere stays the link). Returns 0, or -1 with the Windows error set.
 */
static int load_details(Entry* e, int follow) {
    HANDLE h;
    BY_HANDLE_FILE_INFORMATION info;
    FILE_STANDARD_INFO standard;
    FILE_BASIC_INFO basic;
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    char text[VPATH_MAX];
    const char* error;

    if (e->native == NULL || e->detailed)
        return 0;
    h = CreateFileA(e->native, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, flags | (follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT), NULL);
    if (h == INVALID_HANDLE_VALUE && follow)
        h = CreateFileA(e->native, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, flags | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    if (GetFileInformationByHandle(h, &info)) {
        e->attrs = info.dwFileAttributes;
        e->size = ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
        e->mtime = info.ftLastWriteTime;
        e->atime = info.ftLastAccessTime;
        e->btime = info.ftCreationTime;
        e->ctime = info.ftLastWriteTime;
        e->nlink = info.nNumberOfLinks;
        e->inode = ((unsigned long long)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    }
    if (GetFileInformationByHandleEx(h, FileStandardInfo, &standard, sizeof standard))
        e->alloc = (unsigned long long)standard.AllocationSize.QuadPart;
    else
        e->alloc = (e->size + 4095) / 4096 * 4096;
    if (GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof basic) && basic.ChangeTime.QuadPart != 0) {
        e->ctime.dwLowDateTime = (DWORD)(basic.ChangeTime.QuadPart & 0xFFFFFFFF);
        e->ctime.dwHighDateTime = (DWORD)((unsigned long long)basic.ChangeTime.QuadPart >> 32);
    }
    CloseHandle(h);
    e->detailed = 1;

    e->isLink = (e->attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    if (e->isLink) {
        if (fs_read_link(e->native, text, sizeof text, &error) == 0) {
            DWORD target = GetFileAttributesA(e->native);       // follows the link

            free(e->linkText);
            e->linkText = xstrdup(text);
            e->linkOk = target != INVALID_FILE_ATTRIBUTES;
            e->linkAttrs = e->linkOk ? target : 0;
        } else {
            e->isLink = 0;                                      // a placeholder or the like, not a link
        }
    }
    return 0;
}

/* ---- text: characters, widths, quoting ----------------------------------------------- */

/** The cells a character takes on a terminal. */
static int cell_width(uint32_t c) {
    if (c < 32 || (c >= 0x7F && c < 0xA0))
        return 0;
    if (c >= 0x300 && c <= 0x36F)
        return 0;
    if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) ||
        (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE6F) || (c >= 0xFF00 && c <= 0xFF60) ||
        (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1F64F) || (c >= 0x20000 && c <= 0x3FFFD))
        return 2;
    return 1;
}

static size_t text_width(const char* text) {
    size_t width = 0, left = strlen(text);
    const unsigned char* p = (const unsigned char*)text;

    while (left > 0) {
        size_t length;
        uint32_t c = utf8_decode(p, left, &length);

        width += c >= UTF8_INVALID_BASE ? 4 : (size_t)cell_width(c);
        p += length;
        left -= length;
    }
    return width;
}

/** Non-zero for a character that can be shown as it is: not a control character and not a stray byte. */
static int printable(uint32_t c) {
    return c < UTF8_INVALID_BASE && c >= 32 && c != 127 && !(c >= 0x80 && c < 0xA0);
}

/** With -F and --file-type the characters that end a name with an indicator are quoted in the names. */
static int indicator_char(uint32_t c) {
    return (opt.indicator == IND_CLASSIFY || opt.indicator == IND_FILETYPE) && c < 128 && c != 0 &&
           strchr("*=>@|", (int)c) != NULL;
}

/** Writes the escape of a byte that is not printable, as the C style does: \n for some, else three octal digits. */
static void add_byte_escape(Buf* b, unsigned char c) {
    char piece[8];

    switch (c) {
    case '\a': buf_str(b, "\\a"); return;
    case '\b': buf_str(b, "\\b"); return;
    case '\f': buf_str(b, "\\f"); return;
    case '\n': buf_str(b, "\\n"); return;
    case '\r': buf_str(b, "\\r"); return;
    case '\t': buf_str(b, "\\t"); return;
    case '\v': buf_str(b, "\\v"); return;
    default: break;
    }
    snprintf(piece, sizeof piece, "\\%03o", c);
    buf_str(b, piece);
}

/** Quotes in the C way (also the locale ways, in which the quote is ') */
static void quote_c(Buf* out, const char* name, char quote, int maybe, int* quoted) {
    Buf body = { NULL, 0, 0 };
    size_t left = strlen(name);
    const unsigned char* p = (const unsigned char*)name;
    int changed = 0;

    while (left > 0) {
        size_t length, i;
        uint32_t c = utf8_decode(p, left, &length);

        if (c < UTF8_INVALID_BASE && length == 1 && (char)p[0] == quote) {
            buf_char(&body, '\\');
            buf_char(&body, quote);
            changed = 1;
        } else if (c == '\\') {
            buf_str(&body, "\\\\");
            changed = 1;
        } else if (indicator_char(c)) {
            buf_char(&body, '\\');
            buf_char(&body, (char)c);
            changed = 1;
        } else if (printable(c)) {
            buf_add(&body, (const char*)p, length);
        } else {
            for (i = 0; i < length; i++)
                add_byte_escape(&body, p[i]);
            changed = 1;
        }
        p += length;
        left -= length;
    }
    if (maybe && !changed) {
        buf_add(out, body.data != NULL ? body.data : "", body.length);
        *quoted = 0;
    } else {
        buf_char(out, quote);
        buf_add(out, body.data != NULL ? body.data : "", body.length);
        buf_char(out, quote);
        *quoted = 1;
    }
    buf_free(&body);
}

/** The escape style (-b): backslash, blank and what is not printable are written as escapes. */
static void quote_escape(Buf* out, const char* name) {
    size_t left = strlen(name);
    const unsigned char* p = (const unsigned char*)name;

    while (left > 0) {
        size_t length, i;
        uint32_t c = utf8_decode(p, left, &length);

        if (c == '\\')
            buf_str(out, "\\\\");
        else if (c == ' ')
            buf_str(out, "\\ ");
        else if (indicator_char(c)) {
            buf_char(out, '\\');
            buf_char(out, (char)c);
        } else if (printable(c))
            buf_add(out, (const char*)p, length);
        else
            for (i = 0; i < length; i++)
                add_byte_escape(out, p[i]);
        p += length;
        left -= length;
    }
}

/** The shell styles: the name as it is when that is safe, else inside quotes. */
static void quote_shell(Buf* out, const char* name, Quoting style, int* quoted) {
    int escapes = style == Q_SHELL_ESCAPE || style == Q_SHELL_ESCAPE_ALWAYS;
    int always = style == Q_SHELL_ALWAYS || style == Q_SHELL_ESCAPE_ALWAYS;
    int needs = always || name[0] == '\0';
    int hasQuote = 0, hasSpecial = 0, hasUnprintable = 0;
    Buf text = { NULL, 0, 0 };
    const unsigned char* p;
    size_t left;

    // with -q the control characters are question marks, which the shell would expand
    if (!escapes && opt.hideControl) {
        left = strlen(name);
        p = (const unsigned char*)name;
        while (left > 0) {
            size_t length;
            uint32_t c = utf8_decode(p, left, &length);

            if (printable(c))
                buf_add(&text, (const char*)p, length);
            else
                buf_char(&text, '?');
            p += length;
            left -= length;
        }
        name = text.data != NULL ? text.data : "";
    }

    left = strlen(name);
    p = (const unsigned char*)name;
    while (left > 0) {
        size_t length;
        uint32_t c = utf8_decode(p, left, &length);

        if (c == '\'')
            hasQuote = 1;
        if (c == '"' || c == '$' || c == '`' || c == '\\')
            hasSpecial = 1;
        if (!printable(c) && escapes) {
            hasUnprintable = 1;
            needs = 1;
        }
        if ((c < 128 && c != 0 && strchr(" \t\n!\"$&'()*;<=>?[^`|\\", (int)c) != NULL) || indicator_char(c))
            needs = 1;
        if (p == (const unsigned char*)name && (c == '#' || c == '~'))
            needs = 1;
        p += length;
        left -= length;
    }

    *quoted = needs;
    if (!needs) {
        buf_str(out, name);
    } else if (hasUnprintable) {
        enum { OUTSIDE, SINGLE, DOLLAR } state = SINGLE;

        buf_char(out, '\'');
        left = strlen(name);
        p = (const unsigned char*)name;
        while (left > 0) {
            size_t length, i;
            uint32_t c = utf8_decode(p, left, &length);

            if (!printable(c)) {
                if (state == SINGLE)
                    buf_char(out, '\'');
                if (state != DOLLAR)
                    buf_str(out, "$'");
                state = DOLLAR;
                for (i = 0; i < length; i++)
                    add_byte_escape(out, p[i]);
            } else {
                if (state == DOLLAR) {
                    buf_str(out, "''");
                } else if (state == OUTSIDE) {
                    buf_char(out, '\'');
                }
                state = SINGLE;
                if (c == '\'')
                    buf_str(out, "'\\''");
                else
                    buf_add(out, (const char*)p, length);
            }
            p += length;
            left -= length;
        }
        if (state != OUTSIDE)
            buf_char(out, '\'');
    } else if (hasQuote && !hasSpecial) {
        buf_char(out, '"');
        buf_str(out, name);
        buf_char(out, '"');
    } else {
        buf_char(out, '\'');
        for (; *name != '\0'; name++) {
            if (*name == '\'')
                buf_str(out, "'\\''");
            else
                buf_char(out, *name);
        }
        buf_char(out, '\'');
    }
    buf_free(&text);
}

/**
 * The name as the quoting style writes it. *quoted tells that the shell styles had to put it inside quotes:
 * a column of names is then kept in line by a blank in front of the ones that need none.
 */
static void quote_name(const char* name, Buf* out, int* quoted) {
    size_t left;
    const unsigned char* p;

    *quoted = 0;
    switch (opt.quoting) {
    case Q_LITERAL:
        left = strlen(name);
        p = (const unsigned char*)name;
        while (left > 0) {
            size_t length;
            uint32_t c = utf8_decode(p, left, &length);

            if (opt.hideControl && !printable(c))
                buf_char(out, '?');
            else
                buf_add(out, (const char*)p, length);
            p += length;
            left -= length;
        }
        break;
    case Q_ESCAPE:
        quote_escape(out, name);
        break;
    case Q_C:
    case Q_CLOCALE:
        quote_c(out, name, '"', 0, quoted);
        break;
    case Q_C_MAYBE:
        quote_c(out, name, '"', 1, quoted);
        break;
    case Q_LOCALE:
        quote_c(out, name, '\'', 0, quoted);
        break;
    default:
        quote_shell(out, name, opt.quoting, quoted);
        break;
    }
    // only the styles that leave names bare when they can need the blank for alignment
    if (opt.quoting != Q_SHELL && opt.quoting != Q_SHELL_ESCAPE && opt.quoting != Q_C_MAYBE)
        *quoted = 0;
}

/* ---- colours ---------------------------------------------------------------- */

typedef struct {
    char* suffix;
    char* code;
} ColorExt;

typedef struct {
    char* directory;
    char* link;
    char* program;
    char* file;
    char* orphan;
    int   linkIsTarget;
    ColorExt* extensions;
    size_t    extensionCount, extensionCapacity;
} Colors;

static Colors colors;
static int usedColor;

static void set_color_code(char** slot, const char* value) {
    free(*slot);
    *slot = value[0] != '\0' ? xstrdup(value) : NULL;
}

static void load_colors(void) {
    const char* env = getenv("LS_COLORS");
    char* copy;
    char* item;

    if (env == NULL) {
        set_color_code(&colors.directory, "01;34");
        set_color_code(&colors.link, "01;36");
        set_color_code(&colors.program, "01;32");
        return;
    }
    copy = xstrdup(env);
    for (item = strtok(copy, ":"); item != NULL; item = strtok(NULL, ":")) {
        char* equals = strchr(item, '=');
        const char* value;

        if (equals == NULL)
            continue;
        *equals = '\0';
        value = equals + 1;
        if (item[0] == '*') {
            ColorExt ext;

            if (VEC_RESERVE(colors.extensions, colors.extensionCapacity, colors.extensionCount + 1) != 0)
                out_of_memory();
            ext.suffix = xstrdup(item + 1);
            ext.code = xstrdup(value);
            colors.extensions[colors.extensionCount++] = ext;
        } else if (strcmp(item, "di") == 0) {
            set_color_code(&colors.directory, value);
        } else if (strcmp(item, "ln") == 0) {
            if (strcmp(value, "target") == 0)
                colors.linkIsTarget = 1;
            else
                set_color_code(&colors.link, value);
        } else if (strcmp(item, "ex") == 0) {
            set_color_code(&colors.program, value);
        } else if (strcmp(item, "fi") == 0) {
            set_color_code(&colors.file, value);
        } else if (strcmp(item, "or") == 0) {
            set_color_code(&colors.orphan, value);
        }
    }
    free(copy);
}

/** The colour of a file by its type and name; NULL for none. */
static const char* color_of(const char* name, DWORD attrs, int link, int linkOk, DWORD linkAttrs) {
    size_t i, length = strlen(name);

    if (link) {
        if (!linkOk && colors.orphan != NULL)
            return colors.orphan;
        if (colors.linkIsTarget && linkOk)
            return color_of(name, linkAttrs, 0, 0, 0);
        return colors.link;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        return colors.directory;
    if (name_is_program(name) && colors.program != NULL)
        return colors.program;
    for (i = colors.extensionCount; i > 0; i--) {
        const ColorExt* ext = &colors.extensions[i - 1];
        size_t n = strlen(ext->suffix);

        if (n <= length && strcmp(name + length - n, ext->suffix) == 0)
            return ext->code[0] != '\0' ? ext->code : NULL;
    }
    return colors.file;
}

/* ---- names as shown ------------------------------------------------------------------ */

typedef struct {
    char*  text;
    size_t width;
    int    colored;
} Shown;

static void shown_free(Shown* s) {
    free(s->text);
    s->text = NULL;
}

/** The character that marks the type of a file (-F, -p, --file-type), or '\0'. */
static char type_indicator(DWORD attrs, const char* name, int link) {
    if (link)
        return opt.indicator == IND_CLASSIFY || opt.indicator == IND_FILETYPE ? '@' : '\0';
    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        return opt.indicator != IND_NONE ? '/' : '\0';
    if (opt.indicator == IND_CLASSIFY && name_is_program(name))
        return '*';
    return '\0';
}

/**
 * The text of a name: quoted, coloured, with its indicator. pad puts a blank in front of a name that needs no
 * quotes while others do. mark says whether the indicator is wanted.
 */
static Shown show_name(const char* name, const char* colorName, DWORD attrs, int link, int linkOk, DWORD linkAttrs,
                       int pad, int mark) {
    Buf quoted = { NULL, 0, 0 }, result = { NULL, 0, 0 };
    Shown shown;
    int wasQuoted;
    const char* color = NULL;
    char indicator = mark ? type_indicator(attrs, colorName, link) : '\0';

    quote_name(name, &quoted, &wasQuoted);
    if (opt.color != COLOR_NEVER)
        color = color_of(colorName, attrs, link, linkOk, linkAttrs);
    if (pad && !wasQuoted)
        buf_char(&result, ' ');
    if (color != NULL) {
        buf_str(&result, "\x1b[");
        buf_str(&result, color);
        buf_char(&result, 'm');
    }
    buf_str(&result, quoted.data != NULL ? quoted.data : "");
    if (color != NULL)
        buf_str(&result, "\x1b[0m");
    if (indicator != '\0')
        buf_char(&result, indicator);
    shown.text = result.data != NULL ? result.data : xstrdup("");
    shown.width = text_width(quoted.data != NULL ? quoted.data : "") + (pad && !wasQuoted ? 1 : 0) + (indicator ? 1 : 0);
    shown.colored = color != NULL;
    buf_free(&quoted);
    return shown;
}

static void print_shown(const Shown* s) {
    if (s->colored && !usedColor) {
        fputs("\x1b[0m", stdout);
        usedColor = 1;
    }
    fputs(s->text, stdout);
}

/** Non-zero when a name of the entry would be written in quotes (for the alignment of the others). */
static int name_is_quoted(const Entry* e) {
    Buf quoted = { NULL, 0, 0 };
    int wasQuoted;

    quote_name(e->name, &quoted, &wasQuoted);
    buf_free(&quoted);
    return wasQuoted;
}

static Shown show_entry(const Entry* e, int pad) {
    if (e->isLink && opt.format == FMT_LONG)
        return show_name(e->name, e->name, e->attrs, 1, e->linkOk, e->linkAttrs, pad, 0);   // no @ in the long format
    return show_name(e->name, e->name, e->attrs, e->isLink, e->linkOk, e->linkAttrs, pad, 1);
}

/* ---- numbers ------------------------------------------------------------------- */

static unsigned long long power_of(unsigned long long base, int power) {
    unsigned long long value = 1;
    while (power-- > 0)
        value *= base;
    return value;
}

/** Sizes the 1.5K way: one decimal below 10, rounded up, as GNU ls does. */
static void human_text(unsigned long long n, int base, char* out, size_t size) {
    static const char large[] = "KMGTPE";
    unsigned long long unit = (unsigned long long)base, q, r;
    int power = 1;
    char letter;

    if (n < (unsigned long long)base) {
        snprintf(out, size, "%llu", n);
        return;
    }
    while (power < 6 && n / unit >= (unsigned long long)base) {
        unit *= (unsigned long long)base;
        power++;
    }
    q = n / unit;
    r = n % unit;
    letter = (power == 1 && base == 1000) ? 'k' : large[power - 1];
    if (q < 10) {
        unsigned long long tenths = q * 10 + (r * 10 + unit - 1) / unit;

        if (tenths >= 100)
            snprintf(out, size, "10%c", letter);
        else
            snprintf(out, size, "%llu.%llu%c", tenths / 10, tenths % 10, letter);
    } else {
        unsigned long long whole = q + (r != 0);

        if (whole >= (unsigned long long)base && power < 6)
            snprintf(out, size, "1.0%c", large[power]);
        else
            snprintf(out, size, "%llu%c", whole, letter);
    }
}

static void units_text(const Units* units, unsigned long long bytes, char* out, size_t size) {
    if (units->human) {
        human_text(bytes, units->base, out, size);
    } else if (units->block > 1) {
        unsigned long long n = bytes / units->block + (bytes % units->block != 0);
        snprintf(out, size, "%llu%s", n, units->suffix);
    } else {
        snprintf(out, size, "%llu", bytes);
    }
}

/** Reads a SIZE like 1K, 10MB, 2KiB. Returns 0, or -1. */
static int parse_block_size(const char* text, Units* units) {
    static const char letters[] = "KMGTPE";
    char* end;
    unsigned long long count = 1, multiplier = 1;
    int hasDigits = 0, base = 1024;
    const char* letter;
    char unit = 0;

    if (*text >= '0' && *text <= '9') {
        errno = 0;
        count = strtoull(text, &end, 10);
        if (errno != 0)
            return -1;
        hasDigits = 1;
        text = end;
    }
    if (*text != '\0') {
        letter = strchr(letters, *text >= 'a' && *text <= 'z' ? *text - 32 : *text);
        if (letter == NULL || *letter == '\0')
            return -1;
        unit = *letter;
        text++;
        if (strcmp(text, "iB") == 0) {
            snprintf(units->suffix, sizeof units->suffix, "%ciB", unit);
        } else if (strcmp(text, "B") == 0) {
            base = 1000;
            snprintf(units->suffix, sizeof units->suffix, "%cB", unit == 'K' ? 'k' : unit);
        } else if (*text == '\0') {
            snprintf(units->suffix, sizeof units->suffix, "%c", unit);
        } else {
            return -1;
        }
        multiplier = power_of(base, (int)(letter - letters) + 1);
    } else if (!hasDigits) {
        return -1;
    } else {
        units->suffix[0] = '\0';
    }
    if (hasDigits)
        units->suffix[0] = '\0';        // 1K and 2KB are written as numbers; K and KB as numbers with the unit
    if (count == 0 || (multiplier > 1 && count > ~0ULL / multiplier))
        return -1;
    units->human = 0;
    units->block = count * multiplier;
    return 0;
}

/* ---- times ------------------------------------------------------------------------ */

static FILETIME entry_time(const Entry* e, TimeKind kind) {
    switch (kind) {
    case TIME_ATIME: return e->atime;
    case TIME_CTIME: return e->ctime;
    case TIME_BIRTH: return e->btime;
    default: return e->mtime;
    }
}

static void add_number(Buf* b, long value, int width, char pad) {
    char piece[24];
    snprintf(piece, sizeof piece, "%*ld", width, value);
    if (pad == '0')
        snprintf(piece, sizeof piece, "%0*ld", width, value);
    buf_str(b, piece);
}

/** strftime for the English names; the zone is the local one. */
static void format_with(Buf* out, const char* format, const Instant* when, const struct tm* tm, long offsetSeconds) {

    for (; *format != '\0'; format++) {
        char c = *format;
        char piece[48];

        if (c != '%' || format[1] == '\0') {
            buf_char(out, c);
            continue;
        }
        c = *++format;
        switch (c) {
        case '%': buf_char(out, '%'); break;
        case 'a': buf_add(out, datetime_weekdays[tm->tm_wday], 3); break;
        case 'A': buf_str(out, datetime_weekdays[tm->tm_wday]); break;
        case 'b': case 'h': buf_add(out, datetime_months[tm->tm_mon], 3); break;
        case 'B': buf_str(out, datetime_months[tm->tm_mon]); break;
        case 'c': format_with(out, "%a %b %e %H:%M:%S %Y", when, tm, offsetSeconds); break;
        case 'C': add_number(out, (tm->tm_year + 1900) / 100, 2, '0'); break;
        case 'd': add_number(out, tm->tm_mday, 2, '0'); break;
        case 'D': format_with(out, "%m/%d/%y", when, tm, offsetSeconds); break;
        case 'e': add_number(out, tm->tm_mday, 2, ' '); break;
        case 'F': format_with(out, "%Y-%m-%d", when, tm, offsetSeconds); break;
        case 'H': add_number(out, tm->tm_hour, 2, '0'); break;
        case 'I': add_number(out, tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12, 2, '0'); break;
        case 'j': add_number(out, tm->tm_yday + 1, 3, '0'); break;
        case 'k': add_number(out, tm->tm_hour, 2, ' '); break;
        case 'l': add_number(out, tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12, 2, ' '); break;
        case 'm': add_number(out, tm->tm_mon + 1, 2, '0'); break;
        case 'M': add_number(out, tm->tm_min, 2, '0'); break;
        case 'n': buf_char(out, '\n'); break;
        case 'N': snprintf(piece, sizeof piece, "%09ld", when->nanos); buf_str(out, piece); break;
        case 'p': buf_str(out, tm->tm_hour < 12 ? "AM" : "PM"); break;
        case 'P': buf_str(out, tm->tm_hour < 12 ? "am" : "pm"); break;
        case 'r': format_with(out, "%I:%M:%S %p", when, tm, offsetSeconds); break;
        case 'R': format_with(out, "%H:%M", when, tm, offsetSeconds); break;
        case 's': snprintf(piece, sizeof piece, "%lld", (long long)when->seconds); buf_str(out, piece); break;
        case 'S': add_number(out, tm->tm_sec, 2, '0'); break;
        case 't': buf_char(out, '\t'); break;
        case 'T': format_with(out, "%H:%M:%S", when, tm, offsetSeconds); break;
        case 'u': add_number(out, tm->tm_wday == 0 ? 7 : tm->tm_wday, 1, '0'); break;
        case 'w': add_number(out, tm->tm_wday, 1, '0'); break;
        case 'x': format_with(out, "%m/%d/%y", when, tm, offsetSeconds); break;
        case 'X': format_with(out, "%H:%M:%S", when, tm, offsetSeconds); break;
        case 'y': add_number(out, (tm->tm_year + 1900) % 100, 2, '0'); break;
        case 'Y': add_number(out, tm->tm_year + 1900, 1, '0'); break;
        case 'z': {
            long minutes = offsetSeconds / 60;
            snprintf(piece, sizeof piece, "%c%02ld%02ld", minutes < 0 ? '-' : '+', labs(minutes) / 60, labs(minutes) % 60);
            buf_str(out, piece);
            break;
        }
        case 'Z': {
            const char* name;
            _tzset();
            name = _tzname[tm->tm_isdst > 0 ? 1 : 0];
            buf_str(out, name != NULL ? name : "");
            break;
        }
        default:
            buf_char(out, '%');
            buf_char(out, c);
        }
    }
}

/** The time of a file as the long format shows it (full-iso, long-iso, iso, the default, or +FORMAT). */
static void format_time(FILETIME ft, char* out, size_t size) {
    static const char* defaultStyles = "locale";
    const char* style = opt.timeStyle != NULL ? opt.timeStyle : defaultStyles;
    Instant when = datetime_from_filetime(ft);
    Instant now = datetime_now();
    struct tm tm;
    Buf text = { NULL, 0, 0 };
    const char* oldFormat;
    const char* newFormat;
    char* custom = NULL;
    long offset = 0;
    int recent;

    if (file_time_value(ft) == 0 || datetime_localtime(when.seconds, &tm) != 0) {
        snprintf(out, size, "?");
        return;
    }
    offset = (long)(datetime_gm_seconds(&tm) - when.seconds);
    // six months, as GNU ls counts them
    recent = when.seconds <= now.seconds && now.seconds - when.seconds <= 15778476;

    if (strcmp(style, "full-iso") == 0) {
        oldFormat = newFormat = "%Y-%m-%d %H:%M:%S.%N %z";
    } else if (strcmp(style, "long-iso") == 0) {
        oldFormat = newFormat = "%Y-%m-%d %H:%M";
    } else if (strcmp(style, "iso") == 0) {
        oldFormat = "%Y-%m-%d ";
        newFormat = "%m-%d %H:%M";
    } else if (style[0] == '+') {
        char* newline;

        custom = xstrdup(style + 1);
        newline = strchr(custom, '\n');
        if (newline != NULL) {
            *newline = '\0';
            oldFormat = custom;
            newFormat = newline + 1;
        } else {
            oldFormat = newFormat = custom;
        }
    } else {
        oldFormat = "%b %e  %Y";
        newFormat = "%b %e %H:%M";
    }
    format_with(&text, recent ? newFormat : oldFormat, &when, &tm, offset);
    snprintf(out, size, "%s", text.data != NULL ? text.data : "");
    buf_free(&text);
    free(custom);
}

/* ---- owners ------------------------------------------------------------------------ */

typedef struct {
    PSID sid;
    char text[64];
} SidCache;

/** The name of a SID (or with numeric its RID), with a one entry cache because owners rarely change from file to file. */
static void sid_text(PSID sid, int numeric, SidCache* cache, char* out, size_t size) {
    char name[64], domain[64];
    DWORD nameSize = sizeof name, domainSize = sizeof domain;
    SID_NAME_USE use;

    snprintf(out, size, "?");
    if (sid == NULL)
        return;
    if (numeric) {
        UCHAR count = *GetSidSubAuthorityCount(sid);

        if (count > 0)
            snprintf(out, size, "%lu", (unsigned long)*GetSidSubAuthority(sid, count - 1));
        return;
    }
    if (cache->sid != NULL && EqualSid(cache->sid, sid)) {
        snprintf(out, size, "%s", cache->text);
        return;
    }
    if (LookupAccountSidA(NULL, sid, name, &nameSize, domain, &domainSize, &use)) {
        DWORD length = GetLengthSid(sid);

        snprintf(out, size, "%s", name);
        snprintf(cache->text, sizeof cache->text, "%s", name);
        free(cache->sid);
        cache->sid = malloc(length);
        if (cache->sid != NULL)
            CopySid(length, cache->sid, sid);
    } else {
        // an account that cannot be told by name is shown by its number, as GNU does
        UCHAR count = *GetSidSubAuthorityCount(sid);

        if (count > 0)
            snprintf(out, size, "%lu", (unsigned long)*GetSidSubAuthority(sid, count - 1));
    }
}

static void owner_and_group(const char* path, char* owner, size_t ownerSize, char* group, size_t groupSize) {
    static SidCache ownerCache, groupCache;
    PSID ownerSid = NULL, groupSid = NULL;
    PSECURITY_DESCRIPTOR descriptor = NULL;

    snprintf(owner, ownerSize, "?");
    snprintf(group, groupSize, "?");
    if (path == NULL)
        return;
    if (GetNamedSecurityInfoA((LPSTR)path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
                              &ownerSid, &groupSid, NULL, NULL, &descriptor) != ERROR_SUCCESS)
        return;
    sid_text(ownerSid, opt.numeric, &ownerCache, owner, ownerSize);
    sid_text(groupSid, opt.numeric, &groupCache, group, groupSize);
    LocalFree(descriptor);
}

/* ---- sorting --------------------------------------------------------------------------- */

static int compare_names(const char* a, const char* b) {
    int result = _stricmp(a, b);
    return result != 0 ? result : strcmp(a, b);
}

/** Order of a character in a version: digits are numbers, "~" is before everything, letters by value, the rest after. */
static int version_order(unsigned char c) {
    if (c >= '0' && c <= '9')
        return 0;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        return c;
    if (c == '~')
        return -1;
    return 256 + c;
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

/** GNU's comparison of versions, of the first alen characters of a and the first blen of b. */
static int version_part_compare(const char* a, size_t alen, const char* b, size_t blen) {
    size_t i = 0, j = 0;

    while (i < alen || j < blen) {
        int firstDifference = 0;

        while ((i < alen && !is_digit(a[i])) || (j < blen && !is_digit(b[j]))) {
            int x = i >= alen ? 0 : version_order((unsigned char)a[i]);
            int y = j >= blen ? 0 : version_order((unsigned char)b[j]);

            if (x != y)
                return x - y;
            i++;
            j++;
        }
        while (i < alen && a[i] == '0')
            i++;
        while (j < blen && b[j] == '0')
            j++;
        while (i < alen && j < blen && is_digit(a[i]) && is_digit(b[j])) {
            if (firstDifference == 0)
                firstDifference = a[i] - b[j];
            i++;
            j++;
        }
        if (i < alen && is_digit(a[i]))
            return 1;
        if (j < blen && is_digit(b[j]))
            return -1;
        if (firstDifference != 0)
            return firstDifference;
    }
    return 0;
}

/** The length of a name without its suffixes (.tar.gz): the dot has to be followed by a letter or a tilde. */
static size_t version_prefix_length(const char* s) {
    size_t n = strlen(s), i = 0, prefix = 0;

    for (;;) {
        if (i == n)
            return prefix;
        i++;
        prefix = i;
        while (i + 1 < n && s[i] == '.' && (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '~')) {
            for (i += 2; i < n && (isalnum((unsigned char)s[i]) || s[i] == '~'); i++)
                continue;
        }
    }
}

/** What GNU ls -v does with two names (filevercmp): numbers by value, hidden files first, suffixes counted last. */
static int compare_versions(const char* a, const char* b) {
    int result;

    if (strcmp(a, b) == 0)
        return 0;
    if (*a == '\0')
        return -1;
    if (*b == '\0')
        return 1;
    if (strcmp(a, ".") == 0)
        return -1;
    if (strcmp(b, ".") == 0)
        return 1;
    if (strcmp(a, "..") == 0)
        return -1;
    if (strcmp(b, "..") == 0)
        return 1;
    if (*a == '.' && *b != '.')
        return -1;
    if (*a != '.' && *b == '.')
        return 1;
    if (*a == '.' && *b == '.') {
        a++;
        b++;
    }
    result = version_part_compare(a, version_prefix_length(a), b, version_prefix_length(b));
    if (result == 0)
        result = version_part_compare(a, strlen(a), b, strlen(b));
    return result;
}

static const char* extension_of(const char* name) {
    const char* dot = strrchr(name, '.');
    return dot != NULL ? dot : "";
}

static int compare_entries(void* context, const void* left, const void* right) {
    const Entry* a = left;
    const Entry* b = right;
    int result = 0;

    (void)context;
    if (opt.groupDirectories) {
        int dirA = is_directory(a) && !is_link(a), dirB = is_directory(b) && !is_link(b);

        if (dirA != dirB)
            return dirA ? -1 : 1;
    }
    switch (opt.sort) {
    case SORT_SIZE:
        if (a->size != b->size)
            result = a->size > b->size ? -1 : 1;
        break;
    case SORT_TIME: {
        unsigned long long ta = file_time_value(entry_time(a, opt.timeKind)), tb = file_time_value(entry_time(b, opt.timeKind));

        if (ta != tb)
            result = ta > tb ? -1 : 1;
        break;
    }
    case SORT_VERSION:
        result = compare_versions(a->name, b->name);
        break;
    case SORT_EXT:
        result = strcmp(extension_of(a->name), extension_of(b->name));
        break;
    default:
        break;
    }
    if (result == 0)
        result = compare_names(a->name, b->name);
    return opt.reverse ? -result : result;
}

static void sort_entries(EntryList* list) {
    if (opt.sort != SORT_NONE && list->count > 1)
        qsort_s(list->items, list->count, sizeof *list->items, compare_entries, NULL);
}

/* ---- output ------------------------------------------------------------------------ */

static void format_mode(const Entry* e, char mode[11]) {
    int writable = (e->attrs & FILE_ATTRIBUTE_READONLY) == 0;
    int executable = is_directory(e) || is_program(e);

    mode[0] = is_link(e) ? 'l' : (is_directory(e) ? 'd' : '-');
    mode[1] = 'r';
    mode[2] = writable ? 'w' : '-';
    mode[3] = executable ? 'x' : '-';
    mode[4] = 'r';
    mode[5] = '-';
    mode[6] = executable ? 'x' : '-';
    mode[7] = 'r';
    mode[8] = '-';
    mode[9] = executable ? 'x' : '-';
    mode[10] = '\0';
}

static int console_width(void) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
        return info.srWindow.Right - info.srWindow.Left + 1;
    return 80;
}

static int stdout_is_console(void) {
    DWORD mode;
    return GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) != 0;
}

static long line_width(void) {
    const char* columns;

    if (opt.width >= 0)
        return opt.width;
    if (stdoutIsConsole)
        return console_width();
    columns = getenv("COLUMNS");
    if (columns != NULL && columns[0] >= '0' && columns[0] <= '9') {
        char* end;
        long value = strtol(columns, &end, 10);

        if (*end == '\0' && value > 0)
            return value;
    }
    return 80;
}

/** Moves from column from to column to the way ls does it: tabs where they fit, blanks for the rest. */
static void indent(size_t from, size_t to) {
    // GNU ls uses no tabs with colours (unless -T says so) and none when the line has no limit
    size_t tab = (size_t)opt.tabSize;

    if (line_width() == 0 || (opt.color != COLOR_NEVER && !opt.tabSet))
        tab = 0;

    while (from < to) {
        if (tab != 0 && to / tab > (from + 1) / tab) {
            putchar('\t');
            from += tab - from % tab;
        } else {
            putchar(' ');
            from++;
        }
    }
}

typedef struct {
    int inodeWidth, blockWidth;
} Frills;

static Frills frills_widths(const EntryList* list) {
    Frills f = { 0, 0 };
    size_t i;
    char text[64];

    for (i = 0; i < list->count; i++) {
        const Entry* e = &list->items[i];
        int length;

        if (opt.inode) {
            snprintf(text, sizeof text, "%llu", e->inode);
            length = (int)strlen(text);
            if (length > f.inodeWidth)
                f.inodeWidth = length;
        }
        if (opt.blocks) {
            units_text(&opt.blockUnits, e->alloc, text, sizeof text);
            length = (int)strlen(text);
            if (length > f.blockWidth)
                f.blockWidth = length;
        }
    }
    return f;
}

/** "inode blocks " in front of a name, as -i and -s ask. */
static void add_frills(Buf* b, const Entry* e, Frills f) {
    char text[64], padded[96];

    if (opt.inode) {
        snprintf(text, sizeof text, "%llu", e->inode);
        snprintf(padded, sizeof padded, "%*s ", f.inodeWidth, text);
        buf_str(b, padded);
    }
    if (opt.blocks) {
        units_text(&opt.blockUnits, e->alloc, text, sizeof text);
        snprintf(padded, sizeof padded, "%*s ", f.blockWidth, text);
        buf_str(b, padded);
    }
    if (opt.context)
        buf_str(b, "? ");
}

static size_t frills_length(Frills f) {
    return (opt.inode ? (size_t)f.inodeWidth + 1 : 0) + (opt.blocks ? (size_t)f.blockWidth + 1 : 0) + (opt.context ? 2 : 0);
}

static void print_total(const EntryList* list) {
    unsigned long long total = 0;
    char text[64];
    size_t i;

    for (i = 0; i < list->count; i++)
        total += list->items[i].alloc;
    units_text(&opt.blockUnits, total, text, sizeof text);
    printf("total %s\n", text);
}

static void print_long(const EntryList* list, Frills frills, const Shown* names, int withTotal) {
    size_t i;
    int ownerWidth = 1, groupWidth = 1, authorWidth = 1, linkWidth = 1, sizeWidth = 1;
    char (*owners)[64] = malloc(list->count * sizeof *owners + 1);
    char (*groups)[64] = malloc(list->count * sizeof *groups + 1);
    char (*sizes)[32] = malloc(list->count * sizeof *sizes + 1);
    char sizeText[32];

    if (owners == NULL || groups == NULL || sizes == NULL)
        out_of_memory();

    for (i = 0; i < list->count; i++) {
        const Entry* e = &list->items[i];
        int length;

        owner_and_group(e->native, owners[i], sizeof owners[i], groups[i], sizeof groups[i]);
        length = (int)strlen(owners[i]);
        if (length > ownerWidth)
            ownerWidth = length;
        if (opt.author && length > authorWidth)
            authorWidth = length;
        length = (int)strlen(groups[i]);
        if (length > groupWidth)
            groupWidth = length;
        units_text(&opt.fileUnits, is_directory(e) && !is_link(e) ? 0 : e->size, sizes[i], sizeof sizes[i]);
        length = (int)strlen(sizes[i]);
        if (length > sizeWidth)
            sizeWidth = length;
        snprintf(sizeText, sizeof sizeText, "%lu", (unsigned long)e->nlink);
        length = (int)strlen(sizeText);
        if (length > linkWidth)
            linkWidth = length;
    }
    if (opt.author)
        authorWidth = ownerWidth;

    if (withTotal)
        print_total(list);

    for (i = 0; i < list->count; i++) {
        const Entry* e = &list->items[i];
        char mode[11], timeText[256];
        Buf front = { NULL, 0, 0 };

        add_frills(&front, e, frills);
        format_mode(e, mode);
        format_time(entry_time(e, opt.timeKind), timeText, sizeof timeText);
        printf("%s%s %*lu ", front.data != NULL ? front.data : "", mode, linkWidth, (unsigned long)e->nlink);
        if (opt.owner)
            printf("%-*s ", ownerWidth, owners[i]);
        if (opt.group)
            printf("%-*s ", groupWidth, groups[i]);
        if (opt.author)
            printf("%-*s ", authorWidth, owners[i]);
        if (opt.context)
            fputs("? ", stdout);
        printf("%*s %s ", sizeWidth, sizes[i], timeText);
        print_shown(&names[i]);
        if (is_link(e) && e->linkText != NULL) {
            Shown target = show_name(e->linkText, e->linkText, e->linkAttrs, 0, 0, 0, 0, 1);

            fputs(" -> ", stdout);
            if (e->linkOk) {
                print_shown(&target);
            } else {
                shown_free(&target);
                target = show_name(e->linkText, e->linkText, 0, 0, 0, 0, 0, 0);
                print_shown(&target);
            }
            shown_free(&target);
        }
        putchar('\n');
        buf_free(&front);
    }

    free(owners);
    free(groups);
    free(sizes);
}

/** Lists the items in columns: down each column (-C) or along each line (-x). */
static void print_columns(const Shown* items, const size_t* widths, size_t n, int across) {
    long limit = line_width();
    size_t columns, rows, row, col, i, maxColumns, *columnWidths;

    if (n == 0)
        return;
    columnWidths = malloc(n * sizeof *columnWidths);
    if (columnWidths == NULL)
        out_of_memory();
    maxColumns = limit > 0 ? (size_t)limit / 3 : n;
    if (maxColumns > n)
        maxColumns = n;
    if (maxColumns < 1)
        maxColumns = 1;

    for (columns = maxColumns; columns >= 1; columns--) {
        size_t total = 0;

        rows = (n + columns - 1) / columns;
        // a column count that leaves the last column empty is the same as a smaller one
        if (!across && (columns - 1) * rows >= n && columns > 1)
            continue;
        memset(columnWidths, 0, columns * sizeof *columnWidths);
        for (i = 0; i < n; i++) {
            size_t c = across ? i % columns : i / rows;

            if (widths[i] > columnWidths[c])
                columnWidths[c] = widths[i];
        }
        for (col = 0; col < columns; col++)
            total += columnWidths[col] + (col + 1 < columns ? 2 : 0);
        if (limit == 0 || total < (size_t)limit || columns == 1)
            break;
    }
    rows = (n + columns - 1) / columns;

    for (row = 0; row < rows; row++) {
        size_t position = 0;

        for (col = 0; col < columns; col++) {
            size_t index = across ? row * columns + col : col * rows + row;
            int more;

            if (index >= n)
                break;
            print_shown(&items[index]);
            more = across ? (index + 1 < n && col + 1 < columns) : (col + 1 < columns && (col + 1) * rows + row < n);
            if (more) {
                indent(position + widths[index], position + columnWidths[col] + 2);
                position += columnWidths[col] + 2;
            }
        }
        putchar('\n');
    }
    free(columnWidths);
}

static void print_list(const EntryList* list, int withTotal) {
    size_t i, n = list->count;
    Frills frills = frills_widths(list);
    int align = opt.format == FMT_LONG || opt.format == FMT_COLUMNS || opt.format == FMT_ACROSS;
    int someQuoted = 0;
    Shown* names;
    Shown* items;
    size_t* widths;

    if (align && (opt.quoting == Q_SHELL || opt.quoting == Q_SHELL_ESCAPE || opt.quoting == Q_C_MAYBE)) {
        for (i = 0; i < n && !someQuoted; i++)
            someQuoted = name_is_quoted(&list->items[i]);
    }
    names = calloc(n + 1, sizeof *names);
    if (names == NULL)
        out_of_memory();
    for (i = 0; i < n; i++)
        names[i] = show_entry(&list->items[i], someQuoted);

    if (opt.format == FMT_LONG) {
        print_long(list, frills, names, withTotal);
    } else {
        if (withTotal && opt.blocks)
            print_total(list);
        items = calloc(n + 1, sizeof *items);
        widths = calloc(n + 1, sizeof *widths);
        if (items == NULL || widths == NULL)
            out_of_memory();
        for (i = 0; i < n; i++) {
            Buf text = { NULL, 0, 0 };

            add_frills(&text, &list->items[i], frills);
            buf_str(&text, names[i].text);
            items[i].text = text.data != NULL ? text.data : xstrdup("");
            items[i].colored = names[i].colored;
            widths[i] = frills_length(frills) + names[i].width;
            items[i].width = widths[i];
        }
        if (opt.format == FMT_SINGLE) {
            for (i = 0; i < n; i++) {
                print_shown(&items[i]);
                putchar('\n');
            }
        } else if (opt.format == FMT_COMMAS) {
            long limit = line_width();
            size_t position = 0;

            for (i = 0; i < n; i++) {
                if (i > 0) {
                    if (limit != 0 && position + widths[i] + 2 >= (size_t)limit) {
                        fputs(",\n", stdout);
                        position = 0;
                    } else {
                        fputs(", ", stdout);
                        position += 2;
                    }
                }
                print_shown(&items[i]);
                position += widths[i];
            }
            if (n > 0)
                putchar('\n');
        } else {
            print_columns(items, widths, n, opt.format == FMT_ACROSS);
        }
        for (i = 0; i < n; i++)
            shown_free(&items[i]);
        free(items);
        free(widths);
    }
    for (i = 0; i < n; i++)
        shown_free(&names[i]);
    free(names);
}

/* ---- gathering ------------------------------------------------------------- */

static int is_hidden(const char* name, DWORD attrs) {
    return name[0] == '.' || (attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
}

static int hidden_shown(void) {
    return opt.all || opt.almostAll;
}

static int matches_any(char** patterns, size_t count, const char* name) {
    size_t i;

    for (i = 0; i < count; i++)
        if (glob_match(patterns[i], name))
            return 1;
    return 0;
}

/** Fills in what the listing needs of the entries, then sorts them. */
static void prepare_list(EntryList* list) {
    size_t i;

    if (needs_details()) {
        for (i = 0; i < list->count; i++)
            load_details(&list->items[i], opt.deref);
    }
    sort_entries(list);
}

/** The entries of the virtual root are the drives. */
static void collect_root(EntryList* list) {
    char letters[27];
    size_t count = vpath_drives(letters), i;

    if (opt.all) {
        list_add(list, ".", NULL, FILE_ATTRIBUTE_DIRECTORY, 0, zero_time());
        list_add(list, "..", NULL, FILE_ATTRIBUTE_DIRECTORY, 0, zero_time());
    }
    for (i = 0; i < count; i++) {
        char name[2], native[8];
        WIN32_FILE_ATTRIBUTE_DATA data;
        FILETIME mtime = zero_time();

        name[0] = letters[i];
        name[1] = '\0';
        snprintf(native, sizeof native, "%c:\\", letters[i] - 32);
        if (GetFileAttributesExA(native, GetFileExInfoStandard, &data))
            mtime = data.ftLastWriteTime;
        list_add(list, name, native, FILE_ATTRIBUTE_DIRECTORY, 0, mtime);
    }
}

/** Adds "." and ".." of the directory at the Windows path, with the time and owner of the directories they stand for. */
static void add_dot_entries(EntryList* list, const char* native) {
    char parent[MAX_PATH * 2];
    size_t length;
    char* separator;
    WIN32_FILE_ATTRIBUTE_DATA data;
    FILETIME mtime = zero_time();

    if (GetFileAttributesExA(native, GetFileExInfoStandard, &data))
        mtime = data.ftLastWriteTime;
    list_add(list, ".", native, FILE_ATTRIBUTE_DIRECTORY, 0, mtime);

    // the parent: cut the last component; the parent of a drive root is the root itself
    snprintf(parent, sizeof parent, "%s", native);
    length = strlen(parent);
    while (length > 3 && (parent[length - 1] == '\\' || parent[length - 1] == '/'))
        parent[--length] = '\0';
    separator = strrchr(parent, '\\');
    if (separator != NULL && separator > parent + 2)
        *separator = '\0';
    else if (separator != NULL)
        separator[1] = '\0';        // "C:\dir" -> "C:\"
    mtime = zero_time();
    if (GetFileAttributesExA(parent, GetFileExInfoStandard, &data))
        mtime = data.ftLastWriteTime;
    list_add(list, "..", parent, FILE_ATTRIBUTE_DIRECTORY, 0, mtime);
}

/** Reads the directory at the Windows path. Returns 0, or -1 with errno set. */
static int collect_directory(const char* native, EntryList* list) {
    char pattern[MAX_PATH * 2];
    WIN32_FIND_DATAA data;
    HANDLE find;
    size_t length = strlen(native);
    int needSeparator = length > 0 && native[length - 1] != '\\' && native[length - 1] != '/';

    snprintf(pattern, sizeof pattern, "%s%s*", native, needSeparator ? "\\" : "");
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        errno = (error == ERROR_ACCESS_DENIED) ? EACCES : ENOENT;
        return -1;
    }

    if (opt.all)
        add_dot_entries(list, native);

    do {
        char full[MAX_PATH * 2];
        unsigned long long size;
        Entry e;

        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
            continue;       // the real ones are added above, with -a
        if (!hidden_shown() && is_hidden(data.cFileName, data.dwFileAttributes))
            continue;
        if (matches_any(opt.ignore, opt.ignoreCount, data.cFileName))
            continue;
        if (!hidden_shown() && matches_any(opt.hide, opt.hideCount, data.cFileName))
            continue;

        snprintf(full, sizeof full, "%s%s%s", native, needSeparator ? "\\" : "", data.cFileName);
        size = ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        entry_init(&e, data.cFileName, full);
        e.attrs = data.dwFileAttributes;
        e.size = size;
        e.mtime = e.ctime = data.ftLastWriteTime;
        e.atime = data.ftLastAccessTime;
        e.btime = data.ftCreationTime;
        e.isLink = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        list_push(list, &e);
    } while (FindNextFileA(find, &data));

    FindClose(find);
    return 0;
}

static void join_display(const char* directory, const char* name, char* out, size_t size) {
    size_t length = strlen(directory);
    if (length > 0 && directory[length - 1] == '/')
        snprintf(out, size, "%s%s", directory, name);
    else
        snprintf(out, size, "%s/%s", directory, name);
}

/** The name of a directory in a header: quoted as names are. */
static void print_header(const char* display) {
    Buf quoted = { NULL, 0, 0 };
    int wasQuoted;

    quote_name(display, &quoted, &wasQuoted);
    printf("%s:\n", quoted.data != NULL ? quoted.data : "");
    buf_free(&quoted);
}

/**
 * Lists one directory. display is the name to show in headers and to build
 * the names of subdirectories from; native is NULL for the virtual root.
 */
static void list_directory(Listing* listing, const char* display, const char* native, int header, int topLevel) {
    EntryList list = { NULL, 0, 0 };
    size_t i;

    if (native == NULL) {
        collect_root(&list);
    } else if (collect_directory(native, &list) != 0) {
        if (!listing->firstBlock) {
            putchar('\n');
        }
        if (header)
            print_header(display);
        listing->firstBlock = 0;
        fprintf(stderr, PROGRAM ": cannot open directory '%s': %s\n", display, strerror(errno));
        listing->exitStatus = listing->exitStatus > (topLevel ? 1 : 0) ? listing->exitStatus : (topLevel ? 2 : 1);
        return;
    }

    prepare_list(&list);

    if (!listing->firstBlock)
        putchar('\n');
    listing->firstBlock = 0;
    if (header)
        print_header(display);
    print_list(&list, 1);

    if (opt.recursive) {
        for (i = 0; i < list.count; i++) {
            const Entry* e = &list.items[i];
            char childDisplay[MAX_PATH * 2];

            if (!is_directory(e) || is_link(e) ||
                strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0)
                continue;
            join_display(display, e->name, childDisplay, sizeof childDisplay);
            list_directory(listing, childDisplay, e->native, 1, 0);
        }
    }

    list_free(&list);
}

/* ---- options --------------------------------------------------------------------- */

// the id of an option is its letter, except for the options that have no short one
enum {
    OPT_COLOR = 256, OPT_AUTHOR, OPT_BLOCK_SIZE, OPT_FORMAT, OPT_FULL_TIME, OPT_GROUP_DIRS_FIRST, OPT_HIDE,
    OPT_INDICATOR_STYLE, OPT_FILE_TYPE, OPT_QUOTING_STYLE, OPT_SHOW_CONTROL, OPT_SI, OPT_SORT, OPT_TIME,
    OPT_TIME_STYLE, OPT_DEREF_DIR
};

static const ArgsOption optionList[] = {
    { 'a', 'a', "all",                    ARGS_NONE },
    { 'A', 'A', "almost-all",             ARGS_NONE },
    { OPT_AUTHOR, 0, "author",            ARGS_NONE },
    { 'b', 'b', "escape",                 ARGS_NONE },
    { OPT_BLOCK_SIZE, 0, "block-size",    ARGS_REQUIRED },
    { 'B', 'B', "ignore-backups",         ARGS_NONE },
    { 'c', 'c', NULL,                     ARGS_NONE },
    { 'C', 'C', NULL,                     ARGS_NONE },
    { OPT_COLOR, 0, "color",              ARGS_OPTIONAL },
    { 'd', 'd', "directory",              ARGS_NONE },
    { 'f', 'f', NULL,                     ARGS_NONE },
    { 'F', 'F', "classify",               ARGS_NONE },
    { OPT_FILE_TYPE, 0, "file-type",      ARGS_NONE },
    { OPT_FORMAT, 0, "format",            ARGS_REQUIRED },
    { OPT_FULL_TIME, 0, "full-time",      ARGS_NONE },
    { 'g', 'g', NULL,                     ARGS_NONE },
    { OPT_GROUP_DIRS_FIRST, 0, "group-directories-first", ARGS_NONE },
    { 'G', 'G', "no-group",               ARGS_NONE },
    { 'h', 'h', "human-readable",         ARGS_NONE },
    { OPT_SI, 0, "si",                    ARGS_NONE },
    { 'H', 'H', "dereference-command-line", ARGS_NONE },
    { OPT_DEREF_DIR, 0, "dereference-command-line-symlink-to-dir", ARGS_NONE },
    { OPT_HIDE, 0, "hide",                ARGS_REQUIRED },
    { OPT_INDICATOR_STYLE, 0, "indicator-style", ARGS_REQUIRED },
    { 'i', 'i', "inode",                  ARGS_NONE },
    { 'I', 'I', "ignore",                 ARGS_REQUIRED },
    { 'k', 'k', "kibibytes",              ARGS_NONE },
    { 'l', 'l', NULL,                     ARGS_NONE },
    { 'L', 'L', "dereference",            ARGS_NONE },
    { 'm', 'm', NULL,                     ARGS_NONE },
    { 'n', 'n', "numeric-uid-gid",        ARGS_NONE },
    { 'N', 'N', "literal",                ARGS_NONE },
    { 'o', 'o', NULL,                     ARGS_NONE },
    { 'p', 'p', NULL,                     ARGS_NONE },
    { 'q', 'q', "hide-control-chars",     ARGS_NONE },
    { OPT_SHOW_CONTROL, 0, "show-control-chars", ARGS_NONE },
    { 'Q', 'Q', "quote-name",             ARGS_NONE },
    { OPT_QUOTING_STYLE, 0, "quoting-style", ARGS_REQUIRED },
    { 'r', 'r', "reverse",                ARGS_NONE },
    { 'R', 'R', "recursive",              ARGS_NONE },
    { 's', 's', "size",                   ARGS_NONE },
    { 'S', 'S', NULL,                     ARGS_NONE },
    { OPT_SORT, 0, "sort",                ARGS_REQUIRED },
    { OPT_TIME, 0, "time",                ARGS_REQUIRED },
    { OPT_TIME_STYLE, 0, "time-style",    ARGS_REQUIRED },
    { 't', 't', NULL,                     ARGS_NONE },
    { 'T', 'T', "tabsize",                ARGS_REQUIRED },
    { 'u', 'u', NULL,                     ARGS_NONE },
    { 'U', 'U', NULL,                     ARGS_NONE },
    { 'v', 'v', NULL,                     ARGS_NONE },
    { 'w', 'w', "width",                  ARGS_REQUIRED },
    { 'x', 'x', NULL,                     ARGS_NONE },
    { 'X', 'X', NULL,                     ARGS_NONE },
    { 'Z', 'Z', "context",                ARGS_NONE },
    { 'D', 'D', "dired",                  ARGS_NONE },
    { '1', '1', NULL,                     ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, optionList, 2, 0, 0 };

typedef struct {
    const char* word;
    int         value;
} Word;

/**
 * Finds the value of a word the way GNU does: the whole word, or the start of one word (or of words that agree).
 * Returns the value, or -1 after the message that lists the words.
 */
static int choose_word(const char* option, const char* given, const Word* words) {
    size_t length = strlen(given), i, j;
    int found = -1, ambiguous = 0;

    for (i = 0; words[i].word != NULL; i++) {
        if (strncmp(words[i].word, given, length) != 0)
            continue;
        if (strlen(words[i].word) == length)
            return words[i].value;
        if (found == -1)
            found = words[i].value;
        else if (found != words[i].value)
            ambiguous = 1;
    }
    if (found != -1 && !ambiguous && length > 0)
        return found;
    argumentError = 1;                  // GNU ls leaves with 1 for the words it does not know
    fprintf(stderr, PROGRAM ": %s argument '%s' for '--%s'\nValid arguments are:\n",
            found != -1 && length > 0 ? "ambiguous" : "invalid", given, option);
    for (i = 0; words[i].word != NULL; i++) {
        int first = 1;

        for (j = 0; j < i; j++)
            if (words[j].value == words[i].value)
                first = 0;
        if (!first)
            continue;
        fputs("  - ", stderr);
        for (j = i; words[j].word != NULL; j++) {
            if (words[j].value != words[i].value)
                continue;
            fprintf(stderr, "%s'%s'", j == i ? "" : ", ", words[j].word);
        }
        fputc('\n', stderr);
    }
    args_usage_error(&spec);
    return -1;
}

static const Word formatWords[] = {
    { "verbose", FMT_LONG }, { "long", FMT_LONG }, { "commas", FMT_COMMAS }, { "horizontal", FMT_ACROSS },
    { "across", FMT_ACROSS }, { "vertical", FMT_COLUMNS }, { "single-column", FMT_SINGLE }, { NULL, 0 }
};
static const Word quotingWords[] = {
    { "literal", Q_LITERAL }, { "shell", Q_SHELL }, { "shell-always", Q_SHELL_ALWAYS },
    { "shell-escape", Q_SHELL_ESCAPE }, { "shell-escape-always", Q_SHELL_ESCAPE_ALWAYS }, { "c", Q_C },
    { "c-maybe", Q_C_MAYBE }, { "escape", Q_ESCAPE }, { "locale", Q_LOCALE }, { "clocale", Q_CLOCALE }, { NULL, 0 }
};
static const Word colorWords[] = {
    { "always", COLOR_ALWAYS }, { "yes", COLOR_ALWAYS }, { "force", COLOR_ALWAYS },
    { "never", COLOR_NEVER }, { "no", COLOR_NEVER }, { "none", COLOR_NEVER },
    { "auto", COLOR_AUTO }, { "tty", COLOR_AUTO }, { "if-tty", COLOR_AUTO }, { NULL, 0 }
};
static const Word indicatorWords[] = {
    { "none", IND_NONE }, { "slash", IND_SLASH }, { "file-type", IND_FILETYPE }, { "classify", IND_CLASSIFY }, { NULL, 0 }
};
static const Word sortWords[] = {
    { "none", SORT_NONE }, { "time", SORT_TIME }, { "size", SORT_SIZE }, { "extension", SORT_EXT },
    { "version", SORT_VERSION }, { NULL, 0 }
};
static const Word timeWords[] = {
    { "atime", TIME_ATIME }, { "access", TIME_ATIME }, { "use", TIME_ATIME }, { "ctime", TIME_CTIME },
    { "status", TIME_CTIME }, { "birth", TIME_BIRTH }, { "creation", TIME_BIRTH }, { NULL, 0 }
};

static void add_pattern(char*** patterns, size_t* count, const char* pattern) {
    char** grown = realloc(*patterns, (*count + 1) * sizeof **patterns);

    if (grown == NULL)
        out_of_memory();
    *patterns = grown;
    grown[(*count)++] = xstrdup(pattern);
}

static int set_time_style(const char* style) {
    if (style[0] == '+') {
        opt.timeStyle = xstrdup(style);
        return 0;
    }
    if (strncmp(style, "posix-", 6) == 0) {         // the C locale: the style of the locale
        style += 6;
        if (strcmp(style, "full-iso") == 0 || strcmp(style, "long-iso") == 0 || strcmp(style, "iso") == 0 ||
            strcmp(style, "locale") == 0) {
            opt.timeStyle = NULL;
            return 0;
        }
    }
    if (strcmp(style, "full-iso") == 0 || strcmp(style, "long-iso") == 0 || strcmp(style, "iso") == 0) {
        opt.timeStyle = xstrdup(style);
        return 0;
    }
    if (strcmp(style, "locale") == 0) {
        opt.timeStyle = NULL;
        return 0;
    }
    fprintf(stderr, PROGRAM ": invalid argument '%s' for 'time style'\nValid arguments are:\n"
                    "  - [posix-]full-iso\n  - [posix-]long-iso\n  - [posix-]iso\n  - [posix-]locale\n"
                    "  - +FORMAT (e.g., +%%H:%%M) for a 'date'-style format\n", style);
    args_usage_error(&spec);
    return -1;
}

static void set_format(Format format) {
    opt.format = format;
    opt.formatSet = 1;
}

static void set_sort(SortKey sort) {
    opt.sort = sort;
    opt.sortSet = 1;
}

static void set_human(int base) {
    opt.fileUnits.human = opt.blockUnits.human = 1;
    opt.fileUnits.base = opt.blockUnits.base = base;
}

static int on_option(int id, const char* value, void* context) {
    int chosen;
    char* end;
    long number;

    (void)context;
    switch (id) {
    case 'a': opt.all = 1; opt.almostAll = 0; break;
    case 'A': opt.almostAll = 1; opt.all = 0; break;
    case OPT_AUTHOR: opt.author = 1; break;
    case 'b': opt.quoting = Q_ESCAPE; opt.quotingSet = 1; break;
    case OPT_BLOCK_SIZE:
        if (parse_block_size(value, &opt.blockUnits) != 0) {
            fprintf(stderr, PROGRAM ": invalid --block-size argument '%s'\n", value);
            return -1;
        }
        opt.fileUnits = opt.blockUnits;
        break;
    case 'B': add_pattern(&opt.ignore, &opt.ignoreCount, "*~"); add_pattern(&opt.ignore, &opt.ignoreCount, ".*~"); break;
    case 'c': opt.timeKind = TIME_CTIME; break;
    case 'C': set_format(FMT_COLUMNS); break;
    case OPT_COLOR:
        if (value == NULL) {
            opt.color = COLOR_ALWAYS;
        } else {
            chosen = choose_word("color", value, colorWords);
            if (chosen < 0)
                return -1;
            opt.color = (ColorMode)chosen;
        }
        break;
    case 'd': opt.directory = 1; break;
    case 'f': opt.all = 1; opt.almostAll = 0; set_sort(SORT_NONE); opt.color = COLOR_NEVER; break;
    case 'F': opt.indicator = IND_CLASSIFY; break;
    case OPT_FILE_TYPE: opt.indicator = IND_FILETYPE; break;
    case OPT_FORMAT:
        chosen = choose_word("format", value, formatWords);
        if (chosen < 0)
            return -1;
        set_format((Format)chosen);
        break;
    case OPT_FULL_TIME:
        set_format(FMT_LONG);
        free(opt.timeStyleText);
        opt.timeStyleText = xstrdup("full-iso");
        break;
    case 'g': set_format(FMT_LONG); opt.owner = 0; break;
    case OPT_GROUP_DIRS_FIRST: opt.groupDirectories = 1; break;
    case 'G': opt.group = 0; break;
    case 'h': set_human(1024); break;
    case OPT_SI: set_human(1000); break;
    case 'H': opt.derefArgs = 1; break;
    case OPT_DEREF_DIR: break;
    case OPT_HIDE: add_pattern(&opt.hide, &opt.hideCount, value); break;
    case OPT_INDICATOR_STYLE:
        chosen = choose_word("indicator-style", value, indicatorWords);
        if (chosen < 0)
            return -1;
        opt.indicator = (IndicatorStyle)chosen;
        break;
    case 'i': opt.inode = 1; break;
    case 'I': add_pattern(&opt.ignore, &opt.ignoreCount, value); break;
    case 'k': opt.blockUnits.human = 0; opt.blockUnits.block = 1024; opt.blockUnits.suffix[0] = '\0'; break;
    case 'l': set_format(FMT_LONG); break;
    case 'L': opt.deref = 1; break;
    case 'm': set_format(FMT_COMMAS); break;
    case 'n': set_format(FMT_LONG); opt.numeric = 1; break;
    case 'N': opt.quoting = Q_LITERAL; opt.quotingSet = 1; opt.hideControl = 0; opt.hideControlSet = 1; break;
    case 'o': set_format(FMT_LONG); opt.group = 0; break;
    case 'p': opt.indicator = IND_SLASH; break;
    case 'q': opt.hideControl = 1; opt.hideControlSet = 1; break;
    case OPT_SHOW_CONTROL: opt.hideControl = 0; opt.hideControlSet = 1; break;
    case 'Q': opt.quoting = Q_C; opt.quotingSet = 1; break;
    case OPT_QUOTING_STYLE:
        chosen = choose_word("quoting-style", value, quotingWords);
        if (chosen < 0)
            return -1;
        opt.quoting = (Quoting)chosen;
        opt.quotingSet = 1;
        break;
    case 'r': opt.reverse = 1; break;
    case 'R': opt.recursive = 1; break;
    case 's': opt.blocks = 1; break;
    case 'S': set_sort(SORT_SIZE); break;
    case OPT_SORT:
        chosen = choose_word("sort", value, sortWords);
        if (chosen < 0)
            return -1;
        set_sort((SortKey)chosen);
        break;
    case OPT_TIME:
        chosen = choose_word("time", value, timeWords);
        if (chosen < 0)
            return -1;
        opt.timeKind = (TimeKind)chosen;
        break;
    case OPT_TIME_STYLE:
        free(opt.timeStyleText);
        opt.timeStyleText = xstrdup(value);
        break;
    case 't': set_sort(SORT_TIME); break;
    case 'T':
        errno = 0;
        number = strtol(value, &end, 10);
        if (*value == '\0' || *end != '\0' || errno != 0 || number < 0 || value[0] == '-' || value[0] == '+') {
            fprintf(stderr, PROGRAM ": invalid tab size: '%s'\n", value);
            return -1;
        }
        opt.tabSize = number;
        opt.tabSet = 1;
        break;
    case 'u': opt.timeKind = TIME_ATIME; break;
    case 'U': set_sort(SORT_NONE); break;
    case 'v': set_sort(SORT_VERSION); break;
    case 'w':
        errno = 0;
        number = strtol(value, &end, 10);
        if (*value == '\0' || *end != '\0' || errno != 0 || number < 0 || value[0] == '-' || value[0] == '+') {
            fprintf(stderr, PROGRAM ": invalid line width: '%s'\n", value);
            return -1;
        }
        opt.width = number;
        break;
    case 'x': set_format(FMT_ACROSS); break;
    case 'X': set_sort(SORT_EXT); break;
    case 'Z': opt.context = 1; break;
    case 'D': break;                    // there is no Emacs to tell the places of the names to
    case '1':                           // -1 changes nothing after -l
        if (opt.format != FMT_LONG || !opt.formatSet)
            set_format(FMT_SINGLE);
        break;
    default: break;
    }
    return 0;
}

/** Turns on the colour sequences of a console, which Windows does not read until it is told to. */
static int enable_terminal_colors(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;

    if (!GetConsoleMode(h, &mode))
        return 1;                       // not a console: the escapes go to a file or a pipe as they are
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

/** Puts a command line argument in the lists: files first, then the directories. */
static void add_operand(Listing* listing, EntryList* files, EntryList* directories, const char* path) {
    char native[VPATH_MAX];
    int kind = vpath_to_native(path, native, sizeof native);
    Entry e;
    int follow = opt.deref || opt.derefArgs;

    if (kind == VPATH_ROOT) {
        if (opt.directory)
            list_add(files, path, NULL, FILE_ATTRIBUTE_DIRECTORY, 0, zero_time());
        else
            list_add(directories, path, "", FILE_ATTRIBUTE_DIRECTORY, 0, zero_time());
        return;
    }
    entry_init(&e, path, kind == 0 ? native : "");
    if (kind != 0 || load_details(&e, follow) != 0) {
        fprintf(stderr, PROGRAM ": cannot access '%s': %s\n", path,
                kind != 0 ? "No such file or directory" : fs_strerror(GetLastError()));
        listing->exitStatus = 2;
        entry_free(&e);
        return;
    }
    // a link to a directory is listed as the directory, unless the listing is about the link itself
    if (!follow && e.isLink && e.linkOk && (e.linkAttrs & FILE_ATTRIBUTE_DIRECTORY) &&
        !opt.directory && opt.format != FMT_LONG && opt.indicator != IND_CLASSIFY) {
        e.detailed = 0;
        if (load_details(&e, 1) != 0) {
            entry_free(&e);
            return;
        }
    }
    if (is_directory(&e) && !is_link(&e) && !opt.directory)
        list_push(directories, &e);
    else
        list_push(files, &e);
}

int main(int argc, char* argv[]) {
    const char** operands;
    int operandCount, i, result;
    EntryList files = { NULL, 0, 0 };
    EntryList directories = { NULL, 0, 0 };   // names are the arguments, native the Windows paths ("" for the root)
    Listing listing;
    const char* env;

    memset(&opt, 0, sizeof opt);
    opt.owner = opt.group = 1;
    opt.width = -1;
    opt.tabSize = 8;
    opt.blockUnits.block = 1024;
    opt.blockUnits.base = opt.fileUnits.base = 1024;
    opt.fileUnits.block = 1;
    listing.firstBlock = 1;
    listing.exitStatus = 0;
    stdoutIsConsole = stdout_is_console();

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &operandCount);
    if (result != ARGS_OK)
        return argumentError ? 1 : ARGS_STATUS(spec, result);

    // what the options leave open
    if (!opt.formatSet) {
        if (LS_DEFAULT_FORMAT == 2)
            opt.format = FMT_LONG;
        else if (LS_DEFAULT_FORMAT == 1 || stdoutIsConsole)
            opt.format = FMT_COLUMNS;
        else
            opt.format = FMT_SINGLE;
    }
    if (!opt.quotingSet)
        opt.quoting = LS_DEFAULT_ESCAPE ? Q_ESCAPE : (stdoutIsConsole ? Q_SHELL_ESCAPE : Q_LITERAL);
    if (!opt.hideControlSet)
        opt.hideControl = stdoutIsConsole && !LS_DEFAULT_ESCAPE;
    if (opt.format == FMT_LONG) {
        const char* style = opt.timeStyleText;

        if (style == NULL && (env = getenv("TIME_STYLE")) != NULL && env[0] != '\0')
            style = env;
        if (style != NULL && set_time_style(style) != 0)
            return 2;
    }
    if (!opt.sortSet && opt.timeKind != TIME_MTIME && opt.format != FMT_LONG)
        opt.sort = SORT_TIME;
    if (opt.color == COLOR_AUTO)
        opt.color = stdoutIsConsole ? COLOR_ALWAYS : COLOR_NEVER;
    if (opt.color == COLOR_ALWAYS) {
        if (stdoutIsConsole && !enable_terminal_colors() )
            opt.color = COLOR_NEVER;
        else
            load_colors();
    }

    if (operandCount == 0)
        operands[operandCount++] = ".";

    for (i = 0; i < operandCount; i++)
        add_operand(&listing, &files, &directories, operands[i]);

    // the files given on the command line come first, then the directories
    if (files.count > 0) {
        prepare_list(&files);
        print_list(&files, 0);
        listing.firstBlock = 0;
    }

    sort_entries(&directories);
    for (i = 0; i < (int)directories.count; i++) {
        const Entry* d = &directories.items[i];
        int header = operandCount > 1 || opt.recursive;
        list_directory(&listing, d->name, d->native[0] == '\0' ? NULL : d->native, header, 1);
    }

    list_free(&files);
    list_free(&directories);
    free(operands);
    fflush(stdout);
    return listing.exitStatus;
}
