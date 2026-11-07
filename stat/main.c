/*
 * stat: prints the status of files (or of file systems with -f), as GNU stat does, with Windows meanings for the
 * fields: the device is the serial number of the volume, the inode the file index, the links the number of hard
 * links, the blocks are 512-byte units of the allocated size, the mode follows from the attributes (0644 or 0444
 * for a read-only file, 0755 for a directory and for a program), and the user and group are the numbers (the last
 * part of the SID) and names of the account.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "datetime.h"
#include "fsutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "stat"

static void print_usage(void) {
    puts("Usage: stat [OPTION]... FILE...\n"
         "Display file or file system status.\n"
         "\n"
         "  -L, --dereference     follow links\n"
         "  -f, --file-system     display file system status instead of file status\n"
         "  -c  --format=FORMAT   use the specified FORMAT instead of the default;\n"
         "                          output a newline after each use of FORMAT\n"
         "      --printf=FORMAT   like --format, but interpret backslash escapes,\n"
         "                          and do not output a mandatory trailing newline;\n"
         "                          if you want a newline, include \\n in FORMAT\n"
         "  -t, --terse           print the information in terse form\n"
         "      --help            display this help and exit\n"
         "      --version         output version information and exit\n"
         "\n"
         "The valid format sequences for files (without --file-system):\n"
         "\n"
         "  %a   access rights in octal            %A   access rights in human readable form\n"
         "  %b   number of blocks allocated (see %B)  %B   the size in bytes of each block reported by %b\n"
         "  %d   device number in decimal          %D   device number in hex\n"
         "  %f   raw mode in hex                   %F   file type\n"
         "  %g   group ID of owner                 %G   group name of owner\n"
         "  %h   number of hard links              %i   inode number\n"
         "  %m   mount point                       %n   file name\n"
         "  %N   quoted file name with dereference if symbolic link\n"
         "  %o   optimal I/O transfer size hint    %s   total size, in bytes\n"
         "  %t   major device type in hex          %T   minor device type in hex\n"
         "  %u   user ID of owner                  %U   user name of owner\n"
         "  %w   time of file birth, human-readable; - if unknown\n"
         "  %W   time of file birth, seconds since Epoch; 0 if unknown\n"
         "  %x   time of last access, human-readable    %X   ... seconds since Epoch\n"
         "  %y   time of last data modification         %Y   ... seconds since Epoch\n"
         "  %z   time of last status change             %Z   ... seconds since Epoch\n"
         "\n"
         "Valid format sequences for file systems:\n"
         "\n"
         "  %a   free blocks available to non-superuser  %b   total data blocks in file system\n"
         "  %c   total file nodes in file system         %d   free file nodes in file system\n"
         "  %f   free blocks in file system              %i   file system ID in hex\n"
         "  %l   maximum length of filenames             %n   file name\n"
         "  %s   block size (for faster transfers)       %S   fundamental block size (for block counts)\n"
         "  %t   file system type in hex                 %T   file system type in human readable form");
}

enum { OPT_DEREFERENCE = 1, OPT_FILE_SYSTEM, OPT_FORMAT, OPT_PRINTF, OPT_TERSE };

static const ArgsOption options[] = {
    { OPT_DEREFERENCE, 'L', "dereference", ARGS_NONE },
    { OPT_FILE_SYSTEM, 'f', "file-system", ARGS_NONE },
    { OPT_FORMAT,      'c', "format",      ARGS_REQUIRED },
    { OPT_PRINTF,      0,   "printf",      ARGS_REQUIRED },
    { OPT_TERSE,       't', "terse",       ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int         dereference, fileSystem, terse;
    const char* format;
    int         printfMode;
} Settings;

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;

    switch (id) {
        case OPT_DEREFERENCE: s->dereference = 1; break;
        case OPT_FILE_SYSTEM: s->fileSystem = 1; break;
        case OPT_FORMAT:      s->format = value; s->printfMode = 0; break;
        case OPT_PRINTF:      s->format = value; s->printfMode = 1; break;
        case OPT_TERSE:       s->terse = 1; break;
    }
    return 0;
}

/* ---- the facts about a file -------------------------------------------------------------------- */

typedef struct {
    char               name[VPATH_MAX];       /* as written */
    char               native[VPATH_MAX];
    unsigned long long size;
    unsigned long long allocated;
    unsigned           mode;                  /* with the type bits */
    unsigned long      links;
    unsigned long      volume;
    unsigned long long index;
    Instant            access, modify, change, birth;
    int                isLink;
    char               linkTarget[VPATH_MAX * 2];
    int                isDirectory;
} Facts;

static const char* user_name(void);

static Instant from_large(LONGLONG ticks) {
    FILETIME ft;

    ft.dwLowDateTime = (DWORD)(ticks & 0xffffffff);
    ft.dwHighDateTime = (DWORD)((ULONGLONG)ticks >> 32);
    return datetime_from_filetime(ft);
}

static int is_program(const char* name) {
    const char* dot = strrchr(name, '.');

    return dot != NULL && (_stricmp(dot, ".exe") == 0 || _stricmp(dot, ".bat") == 0 || _stricmp(dot, ".cmd") == 0 ||
                           _stricmp(dot, ".com") == 0 || _stricmp(dot, ".ps1") == 0);
}

/** Reads the facts. Returns 0, or -1 with the Windows error in *error. */
static int read_facts(const char* name, int dereference, Facts* f, DWORD* error) {
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
    FILE_BASIC_INFO basic;
    FILE_STANDARD_INFO standard;
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    int kind;

    memset(f, 0, sizeof *f);
    snprintf(f->name, sizeof f->name, "%s", name);
    kind = vpath_to_native(name, f->native, sizeof f->native);
    if (kind == VPATH_ROOT) {
        // the virtual root: a directory that is only the list of the drives
        f->mode = 040755;
        f->isDirectory = 1;
        f->links = 2;
        {
            Instant now = datetime_now();

            f->access = f->modify = f->change = f->birth = now;
        }
        return 0;
    }
    if (kind != 0) {
        *error = ERROR_PATH_NOT_FOUND;
        return -1;
    }
    if (!dereference)
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    file = CreateFileA(f->native, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, flags, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return -1;
    }
    if (!GetFileInformationByHandle(file, &info)) {
        *error = GetLastError();
        CloseHandle(file);
        return -1;
    }
    memset(&basic, 0, sizeof basic);
    memset(&standard, 0, sizeof standard);
    GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof basic);
    GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof standard);
    CloseHandle(file);

    f->size = ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        f->size = 0;                     /* a directory has no size here, as in GNU tools on Windows */
    f->allocated = standard.AllocationSize.QuadPart > 0 ? (unsigned long long)standard.AllocationSize.QuadPart : 0;
    f->links = info.nNumberOfLinks;
    f->volume = info.dwVolumeSerialNumber;
    f->index = ((unsigned long long)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    f->access = datetime_from_filetime(info.ftLastAccessTime);
    f->modify = datetime_from_filetime(info.ftLastWriteTime);
    f->birth = datetime_from_filetime(info.ftCreationTime);
    f->change = basic.ChangeTime.QuadPart != 0 ? from_large(basic.ChangeTime.QuadPart) : f->modify;
    f->isDirectory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if (!dereference && (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        const char* reason = "";

        if (fs_read_link(f->native, f->linkTarget, sizeof f->linkTarget, &reason) == 0) {
            f->isLink = 1;
            f->mode = 0120777;
            f->isDirectory = 0;
            return 0;
        }
    }
    if (f->isDirectory) {
        f->mode = 040755;
    } else {
        f->mode = 0100000 | ((info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? 0444 : 0644);
        if (is_program(name))
            f->mode |= 0111;
    }
    return 0;
}

static const char* type_name(const Facts* f) {
    if (f->isLink)
        return "symbolic link";
    if (f->isDirectory)
        return "directory";
    return f->size == 0 ? "regular empty file" : "regular file";
}

static void mode_text(unsigned mode, char* out) {
    out[0] = (mode & 0170000) == 040000 ? 'd' : (mode & 0170000) == 0120000 ? 'l' : '-';
    out[1] = (mode & 0400) ? 'r' : '-';
    out[2] = (mode & 0200) ? 'w' : '-';
    out[3] = (mode & 0100) ? 'x' : '-';
    out[4] = (mode & 040) ? 'r' : '-';
    out[5] = (mode & 020) ? 'w' : '-';
    out[6] = (mode & 010) ? 'x' : '-';
    out[7] = (mode & 04) ? 'r' : '-';
    out[8] = (mode & 02) ? 'w' : '-';
    out[9] = (mode & 01) ? 'x' : '-';
    out[10] = '\0';
}

/** The local time as "2026-11-07 19:36:37.246883100 +0900". */
static void time_text(Instant t, char* out, size_t size) {
    struct tm local;
    __int64 offset;
    char sign = '+';

    if (datetime_localtime(t.seconds, &local) != 0) {
        datetime_gmtime(t.seconds, &local);
        offset = 0;
    } else {
        offset = datetime_gm_seconds(&local) - t.seconds;
    }
    if (offset < 0) {
        sign = '-';
        offset = -offset;
    }
    snprintf(out, size, "%04d-%02d-%02d %02d:%02d:%02d.%09ld %c%02d%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
             local.tm_hour, local.tm_min, local.tm_sec, t.nanos, sign, (int)(offset / 3600), (int)(offset / 60 % 60));
}

/* ---- the account ----------------------------------------------------------------------------- */

typedef struct {
    unsigned long userId, groupId;
    char          user[256], group[256];
    int           loaded;
} Account;

static Account account;

static void load_account(void) {
    HANDLE token;
    DWORD size = 0;
    unsigned char buffer[1024];

    if (account.loaded)
        return;
    account.loaded = 1;
    snprintf(account.user, sizeof account.user, "UNKNOWN");
    snprintf(account.group, sizeof account.group, "UNKNOWN");
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return;
    if (GetTokenInformation(token, TokenUser, buffer, sizeof buffer, &size)) {
        PSID sid = ((TOKEN_USER*)buffer)->User.Sid;
        UCHAR count = *GetSidSubAuthorityCount(sid);
        char name[256], domain[256];
        DWORD nameSize = sizeof name, domainSize = sizeof domain;
        SID_NAME_USE use;

        account.userId = *GetSidSubAuthority(sid, count - 1);
        if (LookupAccountSidA(NULL, sid, name, &nameSize, domain, &domainSize, &use))
            snprintf(account.user, sizeof account.user, "%s", name);
    }
    if (GetTokenInformation(token, TokenPrimaryGroup, buffer, sizeof buffer, &size)) {
        PSID sid = ((TOKEN_PRIMARY_GROUP*)buffer)->PrimaryGroup;
        UCHAR count = *GetSidSubAuthorityCount(sid);
        char name[256], domain[256];
        DWORD nameSize = sizeof name, domainSize = sizeof domain;
        SID_NAME_USE use;

        account.groupId = *GetSidSubAuthority(sid, count - 1);
        if (LookupAccountSidA(NULL, sid, name, &nameSize, domain, &domainSize, &use))
            snprintf(account.group, sizeof account.group, "%s", name);
    }
    CloseHandle(token);
}

static const char* user_name(void) {
    load_account();
    return account.user;
}

/* ---- formats --------------------------------------------------------------------------------- */

/** The mount point of the file: the drive as a virtual path (/c). */
static void mount_point(const Facts* f, char* out, size_t size) {
    if (f->native[0] != '\0' && f->native[1] == ':')
        snprintf(out, size, "/%c", (char)tolower((unsigned char)f->native[0]));
    else
        snprintf(out, size, "/");
}

/** Writes text with the printf flags and the width of a directive. */
static void emit_text(const char* flags, int width, const char* text) {
    int length = (int)strlen(text), pad = width > length ? width - length : 0;

    if (strchr(flags, '-') == NULL)
        printf("%*s", pad, "");
    fputs(text, stdout);
    if (strchr(flags, '-') != NULL)
        printf("%*s", pad, "");
}

static void emit_number(const char* flags, int width, unsigned long long value, int base, int upper) {
    char digits[40], text[80];
    const char* alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0, length;

    if (value == 0)
        digits[n++] = '0';
    while (value > 0) {
        digits[n++] = alphabet[value % (unsigned)base];
        value /= (unsigned)base;
    }
    length = 0;
    while (n > 0)
        text[length++] = digits[--n];
    text[length] = '\0';
    if (strchr(flags, '0') != NULL && strchr(flags, '-') == NULL && width > length) {
        int zeros = width - length;

        memmove(text + zeros, text, (size_t)length + 1);
        memset(text, '0', (size_t)zeros);
    }
    emit_text(flags, width, text);
}

static void emit_signed(const char* flags, int width, long long value) {
    char text[40];

    snprintf(text, sizeof text, "%lld", value);
    if (strchr(flags, '0') != NULL && strchr(flags, '-') == NULL && width > (int)strlen(text)) {
        char padded[80];

        snprintf(padded, sizeof padded, "%0*lld", width, value);
        emit_text("", 0, padded);
    } else {
        emit_text(flags, width, text);
    }
}

static void quote_name(const char* name, char* out, size_t size) {
    snprintf(out, size, "'%s'", name);
}

/** One directive for a file. Returns 1 when the letter is known. */
static int file_directive(const Facts* f, char letter, const char* flags, int width) {
    char text[VPATH_MAX * 3];

    load_account();
    switch (letter) {
        case 'a': emit_number(flags, width, f->mode & 07777, 8, 0); break;
        case 'A': mode_text(f->mode, text); emit_text(flags, width, text); break;
        case 'b': emit_number(flags, width, (f->allocated + 511) / 512, 10, 0); break;
        case 'B': emit_number(flags, width, 512, 10, 0); break;
        case 'd': emit_number(flags, width, f->volume, 10, 0); break;
        case 'D': emit_number(flags, width, f->volume, 16, 0); break;
        case 'f': emit_number(flags, width, f->mode, 16, 0); break;
        case 'F': emit_text(flags, width, type_name(f)); break;
        case 'g': emit_number(flags, width, account.groupId, 10, 0); break;
        case 'G': emit_text(flags, width, account.group); break;
        case 'h': emit_number(flags, width, f->links, 10, 0); break;
        case 'i': emit_number(flags, width, f->index, 10, 0); break;
        case 'm': mount_point(f, text, sizeof text); emit_text(flags, width, text); break;
        case 'n': emit_text(flags, width, f->name); break;
        case 'N':
            quote_name(f->name, text, sizeof text);
            if (f->isLink) {
                char target[VPATH_MAX * 2];

                quote_name(f->linkTarget, target, sizeof target);
                strcat(text, " -> ");
                strcat(text, target);
            }
            emit_text(flags, width, text);
            break;
        case 'o': emit_number(flags, width, 4096, 10, 0); break;
        case 's': emit_number(flags, width, f->size, 10, 0); break;
        case 't': case 'T': emit_number(flags, width, 0, 16, 0); break;
        case 'u': emit_number(flags, width, account.userId, 10, 0); break;
        case 'U': emit_text(flags, width, account.user); break;
        case 'w': time_text(f->birth, text, sizeof text); emit_text(flags, width, text); break;
        case 'W': emit_signed(flags, width, f->birth.seconds); break;
        case 'x': time_text(f->access, text, sizeof text); emit_text(flags, width, text); break;
        case 'X': emit_signed(flags, width, f->access.seconds); break;
        case 'y': time_text(f->modify, text, sizeof text); emit_text(flags, width, text); break;
        case 'Y': emit_signed(flags, width, f->modify.seconds); break;
        case 'z': time_text(f->change, text, sizeof text); emit_text(flags, width, text); break;
        case 'Z': emit_signed(flags, width, f->change.seconds); break;
        default:  return 0;
    }
    return 1;
}

typedef struct {
    unsigned long long blockSize, fundamental, total, free, available;
    unsigned long      id, nameLength;
    char               type[64];
    unsigned long      typeNumber;
    char               name[VPATH_MAX];
} FileSystem;

static int file_system_directive(const FileSystem* f, char letter, const char* flags, int width) {
    switch (letter) {
        case 'a': emit_number(flags, width, f->available, 10, 0); break;
        case 'b': emit_number(flags, width, f->total, 10, 0); break;
        case 'c': case 'd': emit_number(flags, width, 0, 10, 0); break;
        case 'f': emit_number(flags, width, f->free, 10, 0); break;
        case 'i': emit_number(flags, width, (unsigned long long)f->id << 32, 16, 0); break;
        case 'l': emit_number(flags, width, f->nameLength, 10, 0); break;
        case 'n': emit_text(flags, width, f->name); break;
        case 's': emit_number(flags, width, f->blockSize, 10, 0); break;
        case 'S': emit_number(flags, width, f->fundamental, 10, 0); break;
        case 't': emit_number(flags, width, f->typeNumber, 16, 0); break;
        case 'T': emit_text(flags, width, f->type); break;
        default:  return 0;
    }
    return 1;
}

static int interpret_escape(const char* p, int* consumed) {
    switch (p[1]) {
        case 'a': *consumed = 2; return 7;
        case 'b': *consumed = 2; return 8;
        case 'e': *consumed = 2; return 27;
        case 'f': *consumed = 2; return 12;
        case 'n': *consumed = 2; return 10;
        case 'r': *consumed = 2; return 13;
        case 't': *consumed = 2; return 9;
        case 'v': *consumed = 2; return 11;
        case '\\': *consumed = 2; return '\\';
        case '"': *consumed = 2; return '"';
        case 'x': {
            int value = 0, n = 0;

            p += 2;
            while (n < 2 && isxdigit((unsigned char)p[n])) {
                value = value * 16 + (isdigit((unsigned char)p[n]) ? p[n] - '0' : tolower((unsigned char)p[n]) - 'a' + 10);
                n++;
            }
            if (n == 0) {
                *consumed = 1;
                return '\\';
            }
            *consumed = 2 + n;
            return value;
        }
        default:
            if (p[1] >= '0' && p[1] <= '7') {
                int value = 0, n = 0;

                p += 1;
                while (n < 3 && p[n] >= '0' && p[n] <= '7') {
                    value = value * 8 + (p[n] - '0');
                    n++;
                }
                *consumed = 1 + n;
                return value;
            }
            // an unknown escape: the character itself, with a warning
            fprintf(stderr, PROGRAM ": warning: unrecognized escape '\\%c'\n", p[1]);
            *consumed = 2;
            return (unsigned char)p[1];
    }
}

/** Writes the format for one file or file system (exactly one of f and fs is not NULL). */
static void print_format(const char* format, int escapes, const Facts* f, const FileSystem* fs) {
    const char* p = format;

    while (*p != '\0') {
        if (*p == '\\' && escapes && p[1] != '\0') {
            int consumed;
            int c = interpret_escape(p, &consumed);

            putchar(c);
            p += consumed;
        } else if (*p == '%') {
            char flags[8];
            int width = 0, n = 0;

            p++;
            if (*p == '%') {
                putchar('%');
                p++;
                continue;
            }
            while (*p != '\0' && strchr("-0+ #'", *p) != NULL && n < 7)
                flags[n++] = *p++;
            flags[n] = '\0';
            while (isdigit((unsigned char)*p))
                width = width * 10 + (*p++ - '0');
            if (*p == '.') {                    /* a precision is accepted and ignored */
                p++;
                while (isdigit((unsigned char)*p))
                    p++;
            }
            if (*p == '\0') {
                putchar('%');
                break;
            }
            if (!(f != NULL ? file_directive(f, *p, flags, width) : file_system_directive(fs, *p, flags, width))) {
                putchar('?');
            }
            p++;
        } else {
            putchar(*p++);
        }
    }
}

/* ---- the default and terse outputs ------------------------------------------------------------ */

static void print_default(const Facts* f) {
    char modeText[16], atime[64], mtime[64], ctime_[64], btime[64], mount[8];

    load_account();
    mode_text(f->mode, modeText);
    time_text(f->access, atime, sizeof atime);
    time_text(f->modify, mtime, sizeof mtime);
    time_text(f->change, ctime_, sizeof ctime_);
    time_text(f->birth, btime, sizeof btime);
    mount_point(f, mount, sizeof mount);
    if (f->isLink)
        printf("  File: %s -> %s\n", f->name, f->linkTarget);
    else
        printf("  File: %s\n", f->name);
    printf("  Size: %-10llu\tBlocks: %-10llu IO Block: %-6d %s\n", f->size, (f->allocated + 511) / 512, 4096, type_name(f));
    printf("Device: %lxh/%lud\tInode: %-11llu  Links: %lu\n", f->volume, f->volume, f->index, f->links);
    printf("Access: (%04o/%s)  Uid: (%5lu/%8s)   Gid: (%5lu/%8s)\n", f->mode & 07777, modeText, account.userId, account.user,
           account.groupId, account.group);
    printf("Access: %s\nModify: %s\nChange: %s\n Birth: %s\n", atime, mtime, ctime_, btime);
}

static const char terseFormat[] = "%n %s %b %f %u %g %D %i %h %t %T %X %Y %Z %W %o";

/* ---- file systems ----------------------------------------------------------------------------- */

static int read_file_system(const char* name, FileSystem* fs) {
    char native[VPATH_MAX], root[8];
    char volumeName[MAX_PATH], typeName[64];
    DWORD serial = 0, maxName = 255, flags = 0, sectors, bytes, freeClusters, clusters;
    ULARGE_INTEGER available, total, free;

    memset(fs, 0, sizeof *fs);
    snprintf(fs->name, sizeof fs->name, "%s", name);
    if (vpath_to_native(name, native, sizeof native) != 0 || GetFileAttributesA(native) == INVALID_FILE_ATTRIBUTES)
        return -1;
    {
        char full[MAX_PATH];

        if (GetFullPathNameA(native, sizeof full, full, NULL) == 0)
            return -1;
        snprintf(root, sizeof root, "%c:\\", full[0]);
    }
    if (!GetVolumeInformationA(root, volumeName, sizeof volumeName, &serial, &maxName, &flags, typeName, sizeof typeName))
        return -1;
    if (!GetDiskFreeSpaceExA(root, &available, &total, &free))
        return -1;
    if (!GetDiskFreeSpaceA(root, &sectors, &bytes, &freeClusters, &clusters)) {
        sectors = 8;
        bytes = 512;
    }
    fs->fundamental = (unsigned long long)sectors * bytes;
    fs->blockSize = fs->fundamental;
    fs->total = total.QuadPart / fs->fundamental;
    fs->free = free.QuadPart / fs->fundamental;
    fs->available = available.QuadPart / fs->fundamental;
    fs->id = serial;
    fs->nameLength = maxName;
    for (clusters = 0; typeName[clusters] != '\0'; clusters++)
        typeName[clusters] = (char)tolower((unsigned char)typeName[clusters]);
    snprintf(fs->type, sizeof fs->type, "%s", typeName);
    fs->typeNumber = strcmp(typeName, "ntfs") == 0 ? 0x5346544e : strcmp(typeName, "fat32") == 0 ? 0x4d44 : 0;
    return 0;
}

static void print_file_system_default(const FileSystem* fs) {
    printf("  File: \"%s\"\n", fs->name);
    printf("    ID: %-8lx00000000 Namelen: %-7lu Type: %s\n", fs->id, fs->nameLength, fs->type);
    printf("Block size: %-10llu Fundamental block size: %llu\n", fs->blockSize, fs->fundamental);
    printf("Blocks: Total: %-10llu Free: %-10llu Available: %llu\n", fs->total, fs->free, fs->available);
    printf("Inodes: Total: %-10d Free: %d\n", 0, 0);
}

int main(int argc, char* argv[]) {
    Settings s;
    const char** operands;
    int count, result, i, status = 0;

    _setmode(_fileno(stdout), _O_BINARY);
    memset(&s, 0, sizeof s);
    result = args_parse(&spec, argc, argv, on_option, &s, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        args_usage_error(&spec);
        return 1;
    }

    for (i = 0; i < count; i++) {
        if (s.fileSystem) {
            FileSystem fs;

            if (read_file_system(operands[i], &fs) != 0) {
                fprintf(stderr, PROGRAM ": cannot read file system information for '%s': No such file or directory\n", operands[i]);
                status = 1;
                continue;
            }
            if (s.format != NULL) {
                print_format(s.format, s.printfMode, NULL, &fs);
                if (!s.printfMode)
                    putchar('\n');
            } else {
                print_file_system_default(&fs);
            }
        } else {
            Facts f;
            DWORD error = 0;

            if (read_facts(operands[i], s.dereference, &f, &error) != 0) {
                fprintf(stderr, PROGRAM ": cannot statx '%s': %s\n", operands[i], fs_strerror(error));
                status = 1;
                continue;
            }
            if (s.format != NULL) {
                print_format(s.format, s.printfMode, &f, NULL);
                if (!s.printfMode)
                    putchar('\n');
            } else if (s.terse) {
                print_format(terseFormat, 0, &f, NULL);
                putchar('\n');
            } else {
                print_default(&f);
            }
        }
    }
    return fflush(stdout) != 0 ? 1 : status;
}
