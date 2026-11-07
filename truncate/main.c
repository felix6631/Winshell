/*
 * truncate: shrinks or extends the size of files, as GNU truncate does. The size is N, +N, -N, <N, >N, /N or %N
 * (set it, extend by, reduce by, at most, at least, round down to a multiple, round up to a multiple); N takes
 * the suffixes K M G T P E (powers of 1024) and KB MB GB ... (powers of 1000). A file that does not exist is
 * created (unless -c); an extension is filled with zero bytes.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "truncate"

static void print_usage(void) {
    puts("Usage: truncate OPTION... FILE...\n"
         "Shrink or extend the size of each FILE to the specified size\n"
         "\n"
         "A FILE argument that does not exist is created.\n"
         "\n"
         "If a FILE is larger than the specified size, the extra data is lost.\n"
         "If a FILE is shorter, it is extended and the sparse extended part (hole)\n"
         "reads as zero bytes.\n"
         "\n"
         "  -c, --no-create        do not create any files\n"
         "  -o, --io-blocks        treat SIZE as number of IO blocks instead of bytes\n"
         "  -r, --reference=RFILE  base size on RFILE\n"
         "  -s, --size=SIZE        set or adjust the file size by SIZE bytes\n"
         "      --help             display this help and exit\n"
         "      --version          output version information and exit\n"
         "\n"
         "The SIZE argument is an integer and optional unit (example: 10K is 10*1024).\n"
         "Units are K,M,G,T,P,E,Z,Y (powers of 1024) or KB,MB,... (powers of 1000).\n"
         "\n"
         "SIZE may also be prefixed by one of the following modifying characters:\n"
         "'+' extend by, '-' reduce by, '<' at most, '>' at least,\n"
         "'/' round down to multiple of, '%' round up to multiple of.");
}

enum { OPT_NO_CREATE = 1, OPT_IO_BLOCKS, OPT_REFERENCE, OPT_SIZE };

static const ArgsOption options[] = {
    { OPT_NO_CREATE, 'c', "no-create",  ARGS_NONE },
    { OPT_IO_BLOCKS, 'o', "io-blocks",  ARGS_NONE },
    { OPT_REFERENCE, 'r', "reference",  ARGS_REQUIRED },
    { OPT_SIZE,      's', "size",       ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int         noCreate, ioBlocks;
    const char* reference;
    int         hasSize;
    char        mode;           // ' ' for an absolute size, else + - < > / %
    long long   size;
} Settings;

/** Reads [+-<>/%]N[suffix]. Returns 0, or -1 after a message. */
static int parse_size(const char* text, char* mode, long long* size) {
    const char* p = text;
    char* end;
    long long value, multiplier = 1;
    int i;

    *mode = ' ';
    if (*p != '\0' && strchr("+-<>/%", *p) != NULL)
        *mode = *p++;
    if (!isdigit((unsigned char)*p)) {
        fprintf(stderr, PROGRAM ": Invalid number: '%s'\n", text);
        return -1;
    }
    errno = 0;
    value = strtoll(p, &end, 10);
    if (errno == ERANGE) {
        fprintf(stderr, PROGRAM ": Invalid number: '%s': Value too large for defined data type\n", text);
        return -1;
    }
    if (*end != '\0') {
        static const char units[] = "KMGTPEZY";
        const char* unit = strchr(units, toupper((unsigned char)end[0]));
        int base = 1024;

        if (unit == NULL || *unit == '\0' || !(end[1] == '\0' || (toupper((unsigned char)end[1]) == 'B' && end[2] == '\0'))) {
            fprintf(stderr, PROGRAM ": Invalid number: '%s'\n", text);
            return -1;
        }
        if (end[1] != '\0')
            base = 1000;
        for (i = 0; i <= (int)(unit - units); i++) {
            if (multiplier > 0x7fffffffffffffffLL / base) {
                fprintf(stderr, PROGRAM ": Invalid number: '%s': Value too large for defined data type\n", text);
                return -1;
            }
            multiplier *= base;
        }
    }
    if (multiplier != 1 && value > 0x7fffffffffffffffLL / multiplier) {
        fprintf(stderr, PROGRAM ": Invalid number: '%s': Value too large for defined data type\n", text);
        return -1;
    }
    if ((*mode == '%' || *mode == '/') && value == 0) {
        fprintf(stderr, PROGRAM ": Invalid number: '%s'\n", text);
        return -1;
    }
    *size = value * multiplier;
    return 0;
}

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;

    switch (id) {
        case OPT_NO_CREATE: s->noCreate = 1; break;
        case OPT_IO_BLOCKS: s->ioBlocks = 1; break;
        case OPT_REFERENCE: s->reference = value; break;
        case OPT_SIZE:
            if (parse_size(value, &s->mode, &s->size) != 0)
                return -1;
            s->hasSize = 1;
            break;
    }
    return 0;
}

/** The size of a file, or -1 after a message. */
static int file_size(const char* display, const char* native, long long* size) {
    HANDLE file = CreateFileA(native, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS, NULL);
    LARGE_INTEGER large;

    if (file == INVALID_HANDLE_VALUE) {
        fprintf(stderr, PROGRAM ": cannot stat '%s': %s\n", display, fs_strerror(GetLastError()));
        return -1;
    }
    if (!GetFileSizeEx(file, &large))
        large.QuadPart = 0;
    CloseHandle(file);
    *size = large.QuadPart;
    return 0;
}

int main(int argc, char* argv[]) {
    Settings s;
    const char** operands;
    int count, result, i, status = 0;
    long long referenceSize = 0;

    memset(&s, 0, sizeof s);
    _setmode(_fileno(stdout), _O_BINARY);
    s.mode = ' ';
    result = args_parse(&spec, argc, argv, on_option, &s, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (!s.hasSize && s.reference == NULL) {
        fprintf(stderr, PROGRAM ": you must specify either '--size' or '--reference'\n");
        args_usage_error(&spec);
        return 1;
    }
    if (s.reference != NULL && s.hasSize && s.mode == ' ') {
        fprintf(stderr, PROGRAM ": you must specify a relative '--size' with '--reference'\n");
        args_usage_error(&spec);
        return 1;
    }
    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing file operand\n");
        args_usage_error(&spec);
        return 1;
    }
    if (s.reference != NULL) {
        char native[VPATH_MAX];

        if (vpath_to_native(s.reference, native, sizeof native) != 0 || file_size(s.reference, native, &referenceSize) != 0)
            return 1;
        if (!s.hasSize) {
            s.mode = ' ';
            s.size = referenceSize;
        }
    }
    if (s.ioBlocks && s.hasSize)
        s.size *= 512;          // the block size of the file system is not asked: 512 bytes

    for (i = 0; i < count; i++) {
        char native[VPATH_MAX];
        HANDLE file;
        LARGE_INTEGER current, target;
        long long base, wanted;

        if (vpath_to_native(operands[i], native, sizeof native) != 0) {
            fprintf(stderr, PROGRAM ": cannot open '%s' for writing: Is a directory\n", operands[i]);
            status = 1;
            continue;
        }
        file = CreateFileA(native, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           s.noCreate ? OPEN_EXISTING : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();

            if (s.noCreate && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND))
                continue;               // -c: a file that is not there is left alone
            fprintf(stderr, PROGRAM ": cannot open '%s' for writing: %s\n", operands[i], fs_strerror(error));
            status = 1;
            continue;
        }
        if (!GetFileSizeEx(file, &current))
            current.QuadPart = 0;
        // every mode is relative to the size of the reference file, or to the size the file has
        base = s.reference != NULL ? referenceSize : current.QuadPart;
        switch (s.mode) {
            case '+': wanted = base + s.size; break;
            case '-': wanted = base - s.size; break;
            case '<': wanted = base < s.size ? base : s.size; break;
            case '>': wanted = base > s.size ? base : s.size; break;
            case '/': wanted = s.size > 0 ? base / s.size * s.size : base; break;
            case '%': wanted = s.size > 0 ? (base + s.size - 1) / s.size * s.size : base; break;
            default:  wanted = s.size; break;
        }
        if (wanted < 0)
            wanted = 0;
        {
            target.QuadPart = wanted;
            if (!SetFilePointerEx(file, target, NULL, FILE_BEGIN) || !SetEndOfFile(file)) {
                fprintf(stderr, PROGRAM ": failed to truncate '%s' at %lld bytes: %s\n", operands[i], wanted,
                        fs_strerror(GetLastError()));
                status = 1;
            }
        }
        CloseHandle(file);
    }
    return status;
}
