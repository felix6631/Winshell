#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#include <aclapi.h>
#include "vpath.h"
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "ls"

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
 *    or a program (.exe .com .bat .cmd). "l" marks reparse points, which
 *    are symbolic links and junctions.
 *  - Owner and group are the ones of the security descriptor of the file.
 *  - Output has LF line endings.
 */

typedef struct {
    int all;            // -a  also show hidden entries, . and ..
    int almostAll;      // -A  also show hidden entries, but not . and ..
    int longFormat;     // -l
    int human;          // -h
    int oneColumn;      // -1
    int forceColumns;   // -C
    int directory;      // -d  list directories themselves
    int recursive;      // -R
    int classify;       // -F  append / * @ to names
    int slashDirs;      // -p  append / to directories
    int reverse;        // -r
    int sortSize;       // -S
    int sortTime;       // -t
    int sortNone;       // -U
} Options;

static Options opts;
static int firstBlock = 1;
static int exitStatus = 0;

typedef struct {
    char*              name;        // as shown
    char*              native;      // Windows path, for the owner; may be NULL
    DWORD              attrs;
    unsigned long long size;
    FILETIME           mtime;
} Entry;

typedef struct {
    Entry* items;
    size_t count, capacity;
} EntryList;

static void print_usage(void) {
    puts("Usage: ls [OPTION]... [FILE]...\n"
         "List information about the FILEs (the current directory by default).\n"
         "\n"
         "  -a, --all                  do not ignore entries starting with .\n"
         "  -A, --almost-all           do not list implied . and ..\n"
         "  -C                         list entries by columns\n"
         "  -d, --directory            list directories themselves, not their contents\n"
         "  -F, --classify             append indicator (one of */@) to entries\n"
         "  -h, --human-readable       with -l, print sizes like 1K 234M 2G\n"
         "  -l                         use a long listing format\n"
         "  -p                         append / indicator to directories\n"
         "  -r, --reverse              reverse order while sorting\n"
         "  -R, --recursive            list subdirectories recursively\n"
         "  -S                         sort by file size, largest first\n"
         "  -t                         sort by modification time, newest first\n"
         "  -U                         do not sort; list entries in directory order\n"
         "  -1                         list one file per line\n"
         "      --color[=WHEN]         accepted and ignored\n"
         "      --help                 display this help and exit\n"
         "      --version              output version information and exit\n"
         "\n"
         "Entries are listed one per line unless standard output is a console.\n"
         "The root directory / holds the drives: ls / shows c, d, ...\n"
         "Names starting with a dot, and files with the hidden or system attribute, are hidden.");
}

/* ---- entries -------------------------------------------------------------- */

static void list_add(EntryList* list, const char* name, const char* native, DWORD attrs,
                     unsigned long long size, FILETIME mtime) {
    Entry* entry;

    if (list->count >= list->capacity) {
        size_t capacity = list->capacity == 0 ? 64 : list->capacity * 2;
        Entry* grown = realloc(list->items, capacity * sizeof *grown);
        if (grown == NULL) {
            fprintf(stderr, PROGRAM ": out of memory\n");
            exit(2);
        }
        list->items = grown;
        list->capacity = capacity;
    }

    entry = &list->items[list->count++];
    entry->name = _strdup(name);
    entry->native = native != NULL ? _strdup(native) : NULL;
    entry->attrs = attrs;
    entry->size = size;
    entry->mtime = mtime;
}

static void list_free(EntryList* list) {
    size_t i;
    for (i = 0; i < list->count; i++) {
        free(list->items[i].name);
        free(list->items[i].native);
    }
    free(list->items);
    list->items = NULL;
    list->count = list->capacity = 0;
}

static unsigned long long file_time_value(FILETIME t) {
    return ((unsigned long long)t.dwHighDateTime << 32) | t.dwLowDateTime;
}

static int is_directory(const Entry* e) {
    return (e->attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/** Reparse points are symbolic links and junctions. They are never followed. */
static int is_link(const Entry* e) {
    return (e->attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

static int has_extension(const char* name, const char* extension) {
    size_t n = strlen(name), e = strlen(extension);
    return n > e && _stricmp(name + n - e, extension) == 0;
}

static int is_program(const Entry* e) {
    return !is_directory(e) &&
           (has_extension(e->name, ".exe") || has_extension(e->name, ".com") ||
            has_extension(e->name, ".bat") || has_extension(e->name, ".cmd"));
}

static int compare_names(const char* a, const char* b) {
    int result = _stricmp(a, b);
    return result != 0 ? result : strcmp(a, b);
}

static int compare_entries(const void* left, const void* right) {
    const Entry* a = left;
    const Entry* b = right;
    int result = 0;

    if (opts.sortSize) {
        if (a->size != b->size)
            result = a->size > b->size ? -1 : 1;
    } else if (opts.sortTime) {
        unsigned long long ta = file_time_value(a->mtime), tb = file_time_value(b->mtime);
        if (ta != tb)
            result = ta > tb ? -1 : 1;
    }
    if (result == 0)
        result = compare_names(a->name, b->name);
    return opts.reverse ? -result : result;
}

static void sort_entries(EntryList* list) {
    if (!opts.sortNone && list->count > 1)
        qsort(list->items, list->count, sizeof *list->items, compare_entries);
}

/* ---- output ------------------------------------------------------------------ */

static char indicator(const Entry* e) {
    if (is_link(e))
        return opts.classify ? '@' : '\0';
    if (is_directory(e))
        return (opts.classify || opts.slashDirs) ? '/' : '\0';
    if (opts.classify && is_program(e))
        return '*';
    return '\0';
}

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

static void format_size(unsigned long long size, char* out, size_t outSize) {
    if (!opts.human || size < 1024) {
        snprintf(out, outSize, "%llu", size);
        return;
    } else {
        static const char units[] = "KMGTPE";
        double value = (double)size;
        int unit = -1;

        while (value >= 1024.0 && unit < 5) {
            value /= 1024.0;
            unit++;
        }
        // like GNU ls, sizes are rounded up
        if (value < 10.0) {
            value = ceil(value * 10.0) / 10.0;
            if (value >= 10.0)
                snprintf(out, outSize, "%.0f%c", value, units[unit]);
            else
                snprintf(out, outSize, "%.1f%c", value, units[unit]);
        } else {
            value = ceil(value);
            if (value >= 1024.0 && unit < 5) {
                snprintf(out, outSize, "1.0%c", units[unit + 1]);
            } else {
                snprintf(out, outSize, "%.0f%c", value, units[unit]);
            }
        }
    }
}

static void format_time(FILETIME mtime, char* out, size_t size) {
    static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    FILETIME local, now;
    SYSTEMTIME t;
    unsigned long long age;
    int recent;

    GetSystemTimeAsFileTime(&now);
    // six months, as GNU ls counts them
    age = file_time_value(now) - file_time_value(mtime);
    recent = file_time_value(now) >= file_time_value(mtime) &&
             age <= 15778476ULL * 10000000ULL;

    FileTimeToLocalFileTime(&mtime, &local);
    if (!FileTimeToSystemTime(&local, &t)) {
        snprintf(out, size, "?");
        return;
    }
    if (recent)
        snprintf(out, size, "%s %2d %02d:%02d", months[t.wMonth - 1], t.wDay, t.wHour, t.wMinute);
    else
        snprintf(out, size, "%s %2d  %4d", months[t.wMonth - 1], t.wDay, t.wYear);
}

/** Names of the owner and the group, with a one entry cache because they rarely change from file to file. */
static void owner_and_group(const char* path, char* owner, size_t ownerSize, char* group, size_t groupSize) {
    static char lastOwner[64], lastGroup[64];
    static PSID lastOwnerSid = NULL, lastGroupSid = NULL;
    PSID ownerSid = NULL, groupSid = NULL;
    PSECURITY_DESCRIPTOR descriptor = NULL;
    char name[64], domain[64];
    DWORD nameSize, domainSize;
    SID_NAME_USE use;

    snprintf(owner, ownerSize, "?");
    snprintf(group, groupSize, "?");
    if (path == NULL)
        return;

    if (GetNamedSecurityInfoA((LPSTR)path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
                              &ownerSid, &groupSid, NULL, NULL, &descriptor) != ERROR_SUCCESS)
        return;

    if (ownerSid != NULL) {
        if (lastOwnerSid != NULL && EqualSid(lastOwnerSid, ownerSid)) {
            snprintf(owner, ownerSize, "%s", lastOwner);
        } else {
            nameSize = sizeof name;
            domainSize = sizeof domain;
            if (LookupAccountSidA(NULL, ownerSid, name, &nameSize, domain, &domainSize, &use)) {
                snprintf(owner, ownerSize, "%s", name);
                snprintf(lastOwner, sizeof lastOwner, "%s", name);
                free(lastOwnerSid);
                lastOwnerSid = malloc(GetLengthSid(ownerSid));
                if (lastOwnerSid != NULL)
                    CopySid(GetLengthSid(ownerSid), lastOwnerSid, ownerSid);
            }
        }
    }
    if (groupSid != NULL) {
        if (lastGroupSid != NULL && EqualSid(lastGroupSid, groupSid)) {
            snprintf(group, groupSize, "%s", lastGroup);
        } else {
            nameSize = sizeof name;
            domainSize = sizeof domain;
            if (LookupAccountSidA(NULL, groupSid, name, &nameSize, domain, &domainSize, &use)) {
                snprintf(group, groupSize, "%s", name);
                snprintf(lastGroup, sizeof lastGroup, "%s", name);
                free(lastGroupSid);
                lastGroupSid = malloc(GetLengthSid(groupSid));
                if (lastGroupSid != NULL)
                    CopySid(GetLengthSid(groupSid), lastGroupSid, groupSid);
            }
        }
    }
    LocalFree(descriptor);
}

static void print_name(const Entry* e) {
    char mark = indicator(e);
    fputs(e->name, stdout);
    if (mark != '\0')
        putchar(mark);
}

static void print_long(const EntryList* list, int withTotal) {
    size_t i;
    int ownerWidth = 1, groupWidth = 1, sizeWidth = 1;
    unsigned long long blocks = 0;
    char (*owners)[64] = malloc(list->count * sizeof *owners + 1);
    char (*groups)[64] = malloc(list->count * sizeof *groups + 1);
    char sizeText[32];

    if (owners == NULL || groups == NULL) {
        fprintf(stderr, PROGRAM ": out of memory\n");
        exit(2);
    }

    for (i = 0; i < list->count; i++) {
        const Entry* e = &list->items[i];
        int length;

        owner_and_group(e->native, owners[i], sizeof owners[i], groups[i], sizeof groups[i]);
        length = (int)strlen(owners[i]);
        if (length > ownerWidth)
            ownerWidth = length;
        length = (int)strlen(groups[i]);
        if (length > groupWidth)
            groupWidth = length;
        format_size(is_directory(e) ? 0 : e->size, sizeText, sizeof sizeText);
        length = (int)strlen(sizeText);
        if (length > sizeWidth)
            sizeWidth = length;
        if (!is_directory(e))
            blocks += (e->size + 4095) / 4096 * 4;      // 1K blocks, as 4K clusters
    }

    if (withTotal) {
        if (opts.human) {
            format_size(blocks * 1024, sizeText, sizeof sizeText);
            printf("total %s\n", sizeText);
        } else {
            printf("total %llu\n", blocks);
        }
    }

    for (i = 0; i < list->count; i++) {
        const Entry* e = &list->items[i];
        char mode[11], timeText[32];

        format_mode(e, mode);
        format_size(is_directory(e) ? 0 : e->size, sizeText, sizeof sizeText);
        format_time(e->mtime, timeText, sizeof timeText);
        printf("%s 1 %-*s %-*s %*s %s ", mode, ownerWidth, owners[i], groupWidth, groups[i],
               sizeWidth, sizeText, timeText);
        print_name(e);
        putchar('\n');
    }

    free(owners);
    free(groups);
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

static size_t shown_length(const Entry* e) {
    return strlen(e->name) + (indicator(e) != '\0' ? 1 : 0);
}

/** Lists the entries by columns, filled top to bottom, as ls does on a terminal. */
static void print_columns(const EntryList* list, int width) {
    size_t n = list->count, columns, rows, row, col, i;
    size_t* widths;

    if (n == 0)
        return;
    widths = malloc(n * sizeof *widths);
    if (widths == NULL)
        return;

    for (columns = n; columns >= 1; columns--) {
        size_t total = 0;
        rows = (n + columns - 1) / columns;
        // a column count that leaves the last column empty is the same as a smaller one
        if ((columns - 1) * rows >= n && columns > 1)
            continue;
        memset(widths, 0, columns * sizeof *widths);
        for (i = 0; i < n; i++) {
            size_t length = shown_length(&list->items[i]);
            size_t c = i / rows;
            if (length > widths[c])
                widths[c] = length;
        }
        for (col = 0; col < columns; col++)
            total += widths[col] + (col + 1 < columns ? 2 : 0);
        if (total <= (size_t)width || columns == 1)
            break;
    }
    rows = (n + columns - 1) / columns;

    for (row = 0; row < rows; row++) {
        for (col = 0; col < columns; col++) {
            size_t index = col * rows + row;
            size_t length;
            if (index >= n)
                break;
            print_name(&list->items[index]);
            length = shown_length(&list->items[index]);
            if (col + 1 < columns && (col + 1) * rows + row < n)
                printf("%*s", (int)(widths[col] - length + 2), "");
        }
        putchar('\n');
    }
    free(widths);
}

static void print_list(const EntryList* list, int withTotal) {
    size_t i;

    if (opts.longFormat) {
        print_long(list, withTotal);
    } else if (opts.oneColumn || (!opts.forceColumns && !stdout_is_console())) {
        for (i = 0; i < list->count; i++) {
            print_name(&list->items[i]);
            putchar('\n');
        }
    } else {
        print_columns(list, console_width());
    }
}

/* ---- gathering ------------------------------------------------------------- */

static int is_hidden(const char* name, DWORD attrs) {
    return name[0] == '.' || (attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
}

static int hidden_shown(void) {
    return opts.all || opts.almostAll;
}

static FILETIME zero_time(void) {
    FILETIME t;
    t.dwLowDateTime = t.dwHighDateTime = 0;
    return t;
}

/** The entries of the virtual root are the drives. */
static void collect_root(EntryList* list) {
    char letters[27];
    size_t count = vpath_drives(letters), i;

    if (opts.all) {
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

    if (opts.all)
        add_dot_entries(list, native);

    do {
        char full[MAX_PATH * 2];
        unsigned long long size;

        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
            continue;       // the real ones are added above, with -a
        if (!hidden_shown() && is_hidden(data.cFileName, data.dwFileAttributes))
            continue;

        snprintf(full, sizeof full, "%s%s%s", native, needSeparator ? "\\" : "", data.cFileName);
        size = ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        list_add(list, data.cFileName, full, data.dwFileAttributes, size, data.ftLastWriteTime);
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

/**
 * Lists one directory. display is the name to show in headers and to build
 * the names of subdirectories from; native is NULL for the virtual root.
 */
static void list_directory(const char* display, const char* native, int header) {
    EntryList list = { NULL, 0, 0 };
    size_t i;

    if (native == NULL) {
        collect_root(&list);
    } else if (collect_directory(native, &list) != 0) {
        if (!firstBlock) {
            putchar('\n');
        }
        if (header)
            printf("%s:\n", display);
        firstBlock = 0;
        fprintf(stderr, PROGRAM ": cannot open directory '%s': %s\n", display, strerror(errno));
        exitStatus = exitStatus > 1 ? exitStatus : 1;
        return;
    }

    sort_entries(&list);

    if (!firstBlock)
        putchar('\n');
    firstBlock = 0;
    if (header)
        printf("%s:\n", display);
    print_list(&list, 1);

    if (opts.recursive) {
        for (i = 0; i < list.count; i++) {
            const Entry* e = &list.items[i];
            char childDisplay[MAX_PATH * 2];

            if (!is_directory(e) || is_link(e) ||
                strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0)
                continue;
            join_display(display, e->name, childDisplay, sizeof childDisplay);
            list_directory(childDisplay, e->native, 1);
        }
    }

    list_free(&list);
}

/* ---- main --------------------------------------------------------------------- */

// the id of an option is its letter, except for the options that have no short one
enum { OPT_COLOR = 256 };

static const ArgsOption optionList[] = {
    { 'a', 'a', "all",            ARGS_NONE },
    { 'A', 'A', "almost-all",     ARGS_NONE },
    { 'C', 'C', NULL,             ARGS_NONE },
    { 'd', 'd', "directory",      ARGS_NONE },
    { 'F', 'F', "classify",       ARGS_NONE },
    { 'h', 'h', "human-readable", ARGS_NONE },
    { 'l', 'l', NULL,             ARGS_NONE },
    { 'p', 'p', NULL,             ARGS_NONE },
    { 'r', 'r', "reverse",        ARGS_NONE },
    { 'R', 'R', "recursive",      ARGS_NONE },
    { 'S', 'S', NULL,             ARGS_NONE },
    { 't', 't', NULL,             ARGS_NONE },
    { 'U', 'U', NULL,             ARGS_NONE },
    { '1', '1', NULL,             ARGS_NONE },
    { OPT_COLOR, 0, "color",      ARGS_OPTIONAL },      // there is no colour output
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, optionList, 2, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    (void)value;
    (void)context;
    switch (id) {
        case 'a': opts.all = 1; break;
        case 'A': opts.almostAll = 1; break;
        case 'C': opts.forceColumns = 1; opts.oneColumn = 0; opts.longFormat = 0; break;
        case 'd': opts.directory = 1; break;
        case 'F': opts.classify = 1; break;
        case 'h': opts.human = 1; break;
        case 'l': opts.longFormat = 1; break;
        case 'p': opts.slashDirs = 1; break;
        case 'r': opts.reverse = 1; break;
        case 'R': opts.recursive = 1; break;
        case 'S': opts.sortSize = 1; opts.sortTime = 0; break;
        case 't': opts.sortTime = 1; opts.sortSize = 0; break;
        case 'U': opts.sortNone = 1; break;
        case '1': opts.oneColumn = 1; opts.forceColumns = 0; break;
        default: break;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int operandCount, i, result;
    EntryList files = { NULL, 0, 0 };
    EntryList directories = { NULL, 0, 0 };   // names are the arguments, native the Windows paths ("" for the root)

    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &operandCount);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    _setmode(_fileno(stdout), _O_BINARY);

    if (operandCount == 0)
        operands[operandCount++] = ".";

    for (i = 0; i < operandCount; i++) {
        const char* path = operands[i];
        char native[VPATH_MAX];
        int kind = vpath_to_native(path, native, sizeof native);
        WIN32_FILE_ATTRIBUTE_DATA data;

        if (kind == VPATH_ROOT) {
            if (opts.directory)
                list_add(&files, path, NULL, FILE_ATTRIBUTE_DIRECTORY, 0, zero_time());
            else
                list_add(&directories, path, "", FILE_ATTRIBUTE_DIRECTORY, 0, zero_time());
            continue;
        }

        if (kind != 0 || !GetFileAttributesExA(native, GetFileExInfoStandard, &data)) {
            fprintf(stderr, PROGRAM ": cannot access '%s': No such file or directory\n", path);
            exitStatus = 2;
            continue;
        }

        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !opts.directory) {
            list_add(&directories, path, native, data.dwFileAttributes, 0, data.ftLastWriteTime);
        } else {
            unsigned long long size = ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
            list_add(&files, path, native, data.dwFileAttributes, size, data.ftLastWriteTime);
        }
    }

    // the files given on the command line come first, then the directories
    if (files.count > 0) {
        sort_entries(&files);
        print_list(&files, 0);
        firstBlock = 0;
    }

    sort_entries(&directories);
    for (i = 0; i < (int)directories.count; i++) {
        const Entry* d = &directories.items[i];
        int header = operandCount > 1 || opts.recursive;
        list_directory(d->name, d->native[0] == '\0' ? NULL : d->native, header);
    }

    list_free(&files);
    list_free(&directories);
    free(operands);
    fflush(stdout);
    return exitStatus;
}
