#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"
#include "fsutil.h"
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "touch"

static void print_usage(void) {
    puts("Usage: touch [OPTION]... FILE...\n"
         "Update the access and modification times of each FILE to the current time.\n"
         "A FILE that does not exist is created empty, unless -c is given.\n"
         "\n"
         "  -a                    change only the access time\n"
         "  -c, --no-create       do not create any files\n"
         "  -d, --date=STRING     use STRING instead of the current time:\n"
         "                          YYYY-MM-DD[ hh:mm[:ss]]  (a T may stand for the space,\n"
         "                          a trailing Z means UTC) or @SECONDS since 1970\n"
         "  -m                    change only the modification time\n"
         "  -r, --reference=FILE  use the times of FILE instead of the current time\n"
         "  -t STAMP              use [[CC]YY]MMDDhhmm[.ss] instead of the current time\n"
         "      --help            display this help and exit\n"
         "      --version         output version information and exit\n"
         "\n"
         "Times given without Z are local times. FILE names are paths of the virtual\n"
         "file system. Directories can be touched as well.");
}

/** Local time fields to a FILETIME, as UTC. Returns 0 on success. */
static int fields_to_filetime(int year, int month, int day, int hour, int minute, int second, int utc, FILETIME* out) {
    SYSTEMTIME t;
    FILETIME local;

    memset(&t, 0, sizeof t);
    t.wYear = (WORD)year;
    t.wMonth = (WORD)month;
    t.wDay = (WORD)day;
    t.wHour = (WORD)hour;
    t.wMinute = (WORD)minute;
    t.wSecond = (WORD)second;

    if (hour > 23 || minute > 59 || second > 60)
        return -1;
    if (second == 60)
        t.wSecond = 59;
    if (!SystemTimeToFileTime(&t, &local))      // also checks the day of the month
        return -1;
    if (utc) {
        *out = local;
        return 0;
    }
    return LocalFileTimeToFileTime(&local, out) ? 0 : -1;
}

static int parse_date(const char* text, FILETIME* out) {
    char buffer[64];
    size_t length;
    int y, mo, d, h = 0, mi = 0, s = 0, utc = 0, count;

    while (*text == ' ')
        text++;

    if (text[0] == '@') {
        char* end;
        unsigned long long seconds = _strtoui64(text + 1, &end, 10);
        unsigned long long ticks;
        if (end == text + 1 || *end != '\0')
            return -1;
        ticks = (seconds + 11644473600ULL) * 10000000ULL;     // 1601 to 1970
        out->dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFFULL);
        out->dwHighDateTime = (DWORD)(ticks >> 32);
        return 0;
    }

    length = strlen(text);
    if (length == 0 || length >= sizeof buffer)
        return -1;
    memcpy(buffer, text, length + 1);
    while (length > 0 && buffer[length - 1] == ' ')
        buffer[--length] = '\0';
    if (length > 0 && (buffer[length - 1] == 'Z' || buffer[length - 1] == 'z')) {
        utc = 1;
        buffer[--length] = '\0';
    }

    {
        // the separator between date and time is a space or a T; one character, then the time
        char* sep = strpbrk(buffer, " T");
        if (sep != NULL)
            *sep = ' ';
    }

    count = sscanf(buffer, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s);
    if (count == 3 || count == 5 || count == 6) {
        // no trailing garbage: the characters used must be all of them
        char check[64];
        if (count == 3)
            snprintf(check, sizeof check, "%d-%d-%d", y, mo, d);
        (void)check;
        return fields_to_filetime(y, mo, d, h, mi, s, utc, out);
    }
    return -1;
}

static int parse_stamp(const char* text, FILETIME* out) {
    // [[CC]YY]MMDDhhmm[.ss]
    char digits[32];
    size_t length, i, n = 0;
    int second = 0, year, month, day, hour, minute;
    const char* dot = strchr(text, '.');
    SYSTEMTIME now;

    length = dot != NULL ? (size_t)(dot - text) : strlen(text);
    if (length != 8 && length != 10 && length != 12)
        return -1;
    for (i = 0; i < length; i++) {
        if (text[i] < '0' || text[i] > '9')
            return -1;
        digits[n++] = text[i];
    }
    digits[n] = '\0';

    if (dot != NULL) {
        if (strlen(dot) != 3 || dot[1] < '0' || dot[1] > '9' || dot[2] < '0' || dot[2] > '9')
            return -1;
        second = (dot[1] - '0') * 10 + (dot[2] - '0');
    }

    GetLocalTime(&now);
    {
        const char* p = digits;
        #define TWO(v) do { (v) = (p[0] - '0') * 10 + (p[1] - '0'); p += 2; } while (0)
        if (length == 12) {
            int century, yy;
            TWO(century);
            TWO(yy);
            year = century * 100 + yy;
        } else if (length == 10) {
            int yy;
            TWO(yy);
            year = yy >= 69 ? 1900 + yy : 2000 + yy;
        } else {
            year = now.wYear;
        }
        TWO(month);
        TWO(day);
        TWO(hour);
        TWO(minute);
        #undef TWO
    }
    return fields_to_filetime(year, month, day, hour, minute, second, 0, out);
}

static const char* error_text(DWORD error) {
    if (error == ERROR_INVALID_NAME)
        return "Invalid argument";
    return fs_strerror(error);
}

static int touch_one(const char* display, const char* native, int noCreate, const FILETIME* access,
                     const FILETIME* modification) {
    HANDLE h;
    DWORD attributes;
    int existed = fs_attributes(native, &attributes) == 0;

    if (!existed && noCreate)
        return 0;

    h = CreateFileA(native, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    noCreate || existed ? OPEN_EXISTING : OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        // creating needs more than the right to write attributes on some file systems
        if (!existed)
            h = CreateFileA(native, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            fprintf(stderr, PROGRAM ": cannot touch '%s': %s\n", display, error_text(GetLastError()));
            return 1;
        }
    }

    if (!SetFileTime(h, NULL, access, modification)) {
        fprintf(stderr, PROGRAM ": setting times of '%s': %s\n", display, error_text(GetLastError()));
        CloseHandle(h);
        return 1;
    }
    CloseHandle(h);
    return 0;
}

enum { OPT_ACCESS = 1, OPT_MODIFICATION, OPT_NO_CREATE, OPT_DATE, OPT_STAMP, OPT_REFERENCE };

static const ArgsOption optionList[] = {
    { OPT_ACCESS,       'a', NULL,         ARGS_NONE },
    { OPT_MODIFICATION, 'm', NULL,         ARGS_NONE },
    { OPT_NO_CREATE,    'c', "no-create",  ARGS_NONE },
    { OPT_DATE,         'd', "date",       ARGS_REQUIRED },
    { OPT_STAMP,        't', NULL,         ARGS_REQUIRED },
    { OPT_REFERENCE,    'r', "reference",  ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, optionList, 1, 0, 0 };

typedef struct {
    int         noCreate, onlyAccess, onlyModification, haveTime;
    const char* referenceFile;
    FILETIME    given;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
        case OPT_ACCESS:       o->onlyAccess = 1; break;
        case OPT_MODIFICATION: o->onlyModification = 1; break;
        case OPT_NO_CREATE:    o->noCreate = 1; break;
        case OPT_REFERENCE:    o->referenceFile = value; break;
        default: {
            int bad = id == OPT_DATE ? parse_date(value, &o->given) : parse_stamp(value, &o->given);
            if (bad != 0) {
                fprintf(stderr, PROGRAM ": invalid date format '%s'\n", value);
                return -1;
            }
            o->haveTime = 1;
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    Options o;
    const char** operands;
    int operandCount, status = 0, i, result;
    FILETIME accessTime, modificationTime, now;
    const FILETIME* access;
    const FILETIME* modification;

    memset(&o, 0, sizeof o);
    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &operandCount);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    // the time to set: the reference file, a given time, or now
    GetSystemTimeAsFileTime(&now);
    accessTime = modificationTime = now;
    if (o.haveTime)
        accessTime = modificationTime = o.given;

    if (o.referenceFile != NULL) {
        char native[VPATH_MAX];
        int kind = vpath_to_native(o.referenceFile, native, sizeof native);
        HANDLE h = INVALID_HANDLE_VALUE;
        FILETIME a, w;

        if (kind == 0)
            h = CreateFileA(native, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (h == INVALID_HANDLE_VALUE || !GetFileTime(h, NULL, &a, &w)) {
            fprintf(stderr, PROGRAM ": failed to get attributes of '%s': %s\n", o.referenceFile,
                    kind == 0 ? fs_strerror(GetLastError()) : "No such file or directory");
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
            return 1;
        }
        CloseHandle(h);
        accessTime = a;
        modificationTime = w;
    }

    // -a and -m choose which of the two is changed; with none of them, both
    access = (o.onlyModification && !o.onlyAccess) ? NULL : &accessTime;
    modification = (o.onlyAccess && !o.onlyModification) ? NULL : &modificationTime;

    for (i = 0; i < operandCount; i++) {
        const char* arg = operands[i];
        char native[VPATH_MAX];
        int kind = vpath_to_native(arg, native, sizeof native);

        if (kind == VPATH_ROOT || kind != 0) {
            if (!o.noCreate) {
                fprintf(stderr, PROGRAM ": cannot touch '%s': Permission denied\n", arg);
                status = 1;
            }
            continue;
        }

        if (touch_one(arg, native, o.noCreate, access, modification) != 0)
            status = 1;
    }

    if (operandCount == 0) {
        fprintf(stderr, PROGRAM ": missing file operand\n");
        fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
        return 1;
    }
    return status;
}
