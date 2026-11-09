/*
 * shred: overwrites files repeatedly so that the data is harder to recover, as GNU shred does: random passes (and,
 * from four passes up, passes of fixed patterns), an optional final pass of zeros, and with -u / --remove the file is
 * renamed to shorter and shorter names of zeros and then deleted. As on Linux a file size is rounded up to the block
 * size (4096) unless -x or -s. Windows has no chmod: -f clears the read-only attribute. Standard output (FILE -) is
 * shredded when it is a regular file.
 */

#define _CRT_RAND_S
#include <ctype.h>
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
#define PROGRAM "shred"
#define BLOCK 4096

typedef unsigned long long u64;

static void print_usage(void) {
    puts("Usage: shred [OPTION]... FILE...\n"
         "Overwrite the specified FILE(s) repeatedly, in order to make it harder\n"
         "for even very expensive hardware probing to recover the data.\n"
         "\n"
         "If FILE is -, shred standard output.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  -f, --force    change permissions to allow writing if necessary\n"
         "  -n, --iterations=N  overwrite N times instead of the default (3)\n"
         "      --random-source=FILE  get random bytes from FILE\n"
         "  -s, --size=N   shred this many bytes (suffixes like K, M, G accepted)\n"
         "  -u             deallocate and remove file after overwriting\n"
         "      --remove[=HOW]  like -u but give control on HOW to delete;  See below\n"
         "  -v, --verbose  show progress\n"
         "  -x, --exact    do not round file sizes up to the next full block;\n"
         "                   this is the default for non-regular files\n"
         "  -z, --zero     add a final overwrite with zeros to hide shredding\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Delete FILE(s) if --remove (-u) is specified.  The default is not to remove\n"
         "the files because it is common to operate on device files like /dev/hda,\n"
         "and those files usually should not be removed.\n"
         "The optional HOW parameter indicates how to remove a directory entry:\n"
         "'unlink' => use a standard unlink call.\n"
         "'wipe' => also first obfuscate bytes in the name.\n"
         "'wipesync' => also sync each obfuscated byte to disk.\n"
         "The default mode is 'wipesync', but note it can be expensive.\n"
         "\n"
         "CAUTION: shred assumes the file system and hardware overwrite data in place.\n"
         "Although this is common, many platforms operate otherwise.  Also, backups\n"
         "and mirrors may contain unremovable copies that will let a shredded file\n"
         "be recovered later.  See the GNU coreutils manual for details.");
}

enum { OPT_FORCE = 1, OPT_ITERATIONS, OPT_RANDOM_SOURCE, OPT_SIZE, OPT_U, OPT_REMOVE, OPT_VERBOSE, OPT_EXACT, OPT_ZERO };

static const ArgsOption options[] = {
    { OPT_FORCE,         'f', "force",         ARGS_NONE },
    { OPT_ITERATIONS,    'n', "iterations",    ARGS_REQUIRED },
    { OPT_RANDOM_SOURCE, 0,   "random-source", ARGS_REQUIRED },
    { OPT_SIZE,          's', "size",          ARGS_REQUIRED },
    { OPT_U,             'u', NULL,            ARGS_NONE },
    { OPT_REMOVE,        0,   "remove",        ARGS_OPTIONAL },
    { OPT_VERBOSE,       'v', "verbose",       ARGS_NONE },
    { OPT_EXACT,         'x', "exact",         ARGS_NONE },
    { OPT_ZERO,          'z', "zero",          ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef enum { REMOVE_NONE, REMOVE_UNLINK, REMOVE_WIPE, REMOVE_WIPESYNC } Remove;

static int force, verbose, exact, zero;
static u64 iterations = 3;
static int sizeGiven;
static u64 sizeArg;
static Remove removeMode = REMOVE_NONE;
static FILE* randomSource;
static const char* randomSourceName;

static const char* quote(const char* text) {
    static char buffer[2048];
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

/** Digits with an optional suffix (b c w K M G T P E, with B for powers of 1000), no overflow. */
static int parse_size(const char* text, u64* out, int* overflow) {
    const char* p = text;
    u64 value = 0, multiplier = 1;
    int any = 0, i;

    *overflow = 0;
    for (; isdigit((unsigned char)*p); p++) {
        any = 1;
        if (value > (~0ULL - (u64)(*p - '0')) / 10)
            *overflow = 1;
        value = value * 10 + (u64)(*p - '0');
    }
    if (!any)
        return -1;
    if (*p != '\0') {
        int base = 1024;
        const char* letters = "KMGTPEZY";
        const char* at;

        if (strcmp(p, "b") == 0) {
            multiplier = 512;
        } else if (strcmp(p, "c") == 0) {
            multiplier = 1;
        } else if (strcmp(p, "w") == 0) {
            multiplier = 2;
        } else {
            at = strchr(letters, p[0] == 'k' ? 'K' : p[0]);
            if (at == NULL || p[0] == '\0')
                return -1;
            if (p[1] == 'B')
                base = 1000;
            else if (p[1] == 'i' && p[2] == 'B')
                base = 1024;
            else if (p[1] != '\0')
                return -1;
            if (p[1] == 'B' && p[2] != '\0')
                return -1;
            for (i = 0; i <= (int)(at - letters); i++) {
                if (multiplier > ~0ULL / (u64)base)
                    *overflow = 1;
                multiplier *= (u64)base;
            }
        }
        if (multiplier != 0 && value > ~0ULL / multiplier)
            *overflow = 1;
        value *= multiplier;
    }
    *out = value;
    return 0;
}

static int on_option(int id, const char* value, void* context) {
    (void)context;
    switch (id) {
    case OPT_FORCE:
        force = 1;
        break;
    case OPT_ITERATIONS: {
        u64 n;
        int overflow;
        const char* p = value;

        if (!isdigit((unsigned char)*p) || parse_size(value, &n, &overflow) != 0 || strpbrk(value, "bcwKMGTPEZYkB") != NULL) {
            fprintf(stderr, PROGRAM ": invalid number of passes: %s\n", quote(value));
            return -1;
        }
        if (overflow) {
            fprintf(stderr, PROGRAM ": invalid number of passes: %s: Value too large for defined data type\n", quote(value));
            return -1;
        }
        iterations = n;
        break;
    }
    case OPT_RANDOM_SOURCE:
        randomSourceName = value;
        break;
    case OPT_SIZE: {
        int overflow;

        if (parse_size(value, &sizeArg, &overflow) != 0) {
            fprintf(stderr, PROGRAM ": invalid file size: %s\n", quote(value));
            return -1;
        }
        if (overflow) {
            fprintf(stderr, PROGRAM ": invalid file size: %s: Value too large for defined data type\n", quote(value));
            return -1;
        }
        sizeGiven = 1;
        break;
    }
    case OPT_U:
        removeMode = REMOVE_WIPESYNC;
        break;
    case OPT_REMOVE:
        if (value == NULL) {
            removeMode = REMOVE_WIPESYNC;
        } else {
            static const char* const names[] = { "unlink", "wipe", "wipesync" };
            size_t length = strlen(value);
            int i, found = -1, ambiguous = 0;

            for (i = 0; i < 3; i++) {
                if (strncmp(names[i], value, length) == 0) {
                    if (strlen(names[i]) == length) {
                        found = i;
                        ambiguous = 0;
                        break;
                    }
                    if (found >= 0)
                        ambiguous = 1;
                    else
                        found = i;
                }
            }
            if (found < 0 || ambiguous) {
                fprintf(stderr, PROGRAM ": %s argument %s for '--remove'\nValid arguments are:\n  - 'unlink'\n  - 'wipe'\n  - 'wipesync'\n",
                        ambiguous ? "ambiguous" : "invalid", quote(value));
                args_usage_error(&spec);
                return -1;
            }
            removeMode = (Remove)(found + 1);
        }
        break;
    case OPT_VERBOSE:
        verbose = 1;
        break;
    case OPT_EXACT:
        exact = 1;
        break;
    case OPT_ZERO:
        zero = 1;
        break;
    default:
        break;
    }
    return 0;
}

/* ---- the random bytes and the patterns ---- */

static void random_bytes(unsigned char* buffer, size_t length) {
    size_t i = 0;

    if (randomSource != NULL) {
        size_t got = fread(buffer, 1, length, randomSource);

        if (got < length) {
            fflush(stdout);
            fprintf(stderr, PROGRAM ": %s: end of file\n", quote(randomSourceName));
            exit(1);
        }
        return;
    }
    while (i < length) {
        unsigned int r;

        if (rand_s(&r) != 0)
            r = (unsigned int)GetTickCount() * 2654435761u + (unsigned int)i;
        for (int k = 0; k < 4 && i < length; k++, i++)
            buffer[i] = (unsigned char)(r >> (8 * k));
    }
}

static const unsigned int patternTable[] = {
    0x000, 0xFFF, 0x555, 0xAAA, 0x249, 0x492, 0x6DB, 0x924, 0xB6D, 0xDB6, 0x111, 0x222,
    0x333, 0x444, 0x666, 0x777, 0x888, 0x999, 0xBBB, 0xCCC, 0xDDD, 0xEEE
};

/** The passes: -1 for a random one, else a 12-bit pattern repeated. */
static void make_passes(int* passes, u64 count) {
    u64 i, randoms;
    unsigned char r;
    unsigned int order[22];
    int k;

    if (count <= 3) {
        for (i = 0; i < count; i++)
            passes[i] = -1;
        return;
    }
    randoms = count / 3 + 1;
    for (k = 0; k < 22; k++)
        order[k] = patternTable[k];
    for (k = 21; k > 0; k--) {                 // a shuffle of the patterns
        int j;
        unsigned int t;

        random_bytes(&r, 1);
        j = r % (k + 1);
        t = order[k];
        order[k] = order[j];
        order[j] = t;
    }
    {
        u64 patternIndex = 0;

        for (i = 0; i < count; i++) {
            int isRandom = i == 0 || i == count - 1 || (i * randoms / count != (i + 1) * randoms / count && i % 3 == 1);

            if (isRandom) {
                passes[i] = -1;
            } else {
                passes[i] = (int)order[patternIndex++ % 22];
            }
        }
    }
}

/* ---- one file ---- */

static int write_pass(HANDLE h, u64 size, int pattern, const char* name) {
    static unsigned char buffer[65536];
    u64 done = 0;
    LARGE_INTEGER zeroPos;

    zeroPos.QuadPart = 0;
    if (!SetFilePointerEx(h, zeroPos, NULL, FILE_BEGIN)) {
        fprintf(stderr, PROGRAM ": %s: error seeking: %s\n", name, fs_strerror(GetLastError()));
        return 0;
    }
    while (done < size) {
        DWORD chunk = (DWORD)(size - done > sizeof buffer ? sizeof buffer : size - done), written;

        if (pattern < 0) {
            random_bytes(buffer, chunk);
        } else {
            unsigned int word = ((unsigned int)pattern << 12) | (unsigned int)pattern;
            unsigned char bytes[3];
            DWORD k;

            bytes[0] = (unsigned char)(word >> 16);
            bytes[1] = (unsigned char)(word >> 8);
            bytes[2] = (unsigned char)word;
            for (k = 0; k < chunk; k++)
                buffer[k] = bytes[(done + k) % 3];
        }
        if (!WriteFile(h, buffer, chunk, &written, NULL) || written != chunk) {
            fprintf(stderr, PROGRAM ": %s: error writing at offset %llu: %s\n", name, done, fs_strerror(GetLastError()));
            return 0;
        }
        done += chunk;
    }
    FlushFileBuffers(h);
    return 1;
}

static void pattern_name(int pattern, char* out, size_t size) {
    if (pattern < 0)
        snprintf(out, size, "random");
    else
        snprintf(out, size, "%03x%03x", (unsigned)pattern, (unsigned)pattern);
}

/** Renames the file to names of zeros, shorter each time, and deletes it. */
static int remove_file(const char* name, const char* native, Remove mode) {
    char directory[VPATH_MAX], base[VPATH_MAX], current[VPATH_MAX], currentDisplay[VPATH_MAX];
    const char* slash;
    size_t length, len;

    if (verbose)
        fprintf(stderr, PROGRAM ": %s: removing\n", name);
    snprintf(current, sizeof current, "%s", native);
    snprintf(currentDisplay, sizeof currentDisplay, "%s", name);
    if (mode != REMOVE_UNLINK) {
        slash = strrchr(native, '\\');
        if (slash == NULL)
            slash = strrchr(native, '/');
        if (slash != NULL) {
            snprintf(directory, sizeof directory, "%.*s", (int)(slash - native + 1), native);
            snprintf(base, sizeof base, "%s", slash + 1);
        } else {
            directory[0] = '\0';
            snprintf(base, sizeof base, "%s", native);
        }
        length = strlen(base);
        for (len = length; len >= 1; len--) {
            char newBase[VPATH_MAX], newNative[VPATH_MAX];
            int tries;

            memset(newBase, '0', len);
            newBase[len] = '\0';
            for (tries = 0; tries < 36; tries++) {
                snprintf(newNative, sizeof newNative, "%s%s", directory, newBase);
                if (GetFileAttributesA(newNative) == INVALID_FILE_ATTRIBUTES)
                    break;
                // a name that is taken: count up in the last place
                {
                    char* last = newBase + len - 1;

                    *last = *last == '9' ? 'a' : (char)(*last + 1);
                }
            }
            if (tries == 36)
                continue;
            if (MoveFileExA(current, newNative, 0)) {
                if (verbose) {
                    const char* shown = strrchr(newNative, '\\') != NULL ? strrchr(newNative, '\\') + 1 : newNative;

                    fprintf(stderr, PROGRAM ": %s: renamed to %s\n", currentDisplay, shown);
                    snprintf(currentDisplay, sizeof currentDisplay, "%s", shown);
                }
                snprintf(current, sizeof current, "%s", newNative);
            }
        }
    }
    if (!DeleteFileA(current)) {
        fprintf(stderr, PROGRAM ": %s: failed to remove: %s\n", name, fs_strerror(GetLastError()));
        return 0;
    }
    if (verbose)
        fprintf(stderr, PROGRAM ": %s: removed\n", name);
    return 1;
}

static int shred_file(const char* name) {
    char native[VPATH_MAX];
    DWORD attributes = 0;
    HANDLE h;
    LARGE_INTEGER fileSize;
    u64 size;
    int isStdout = strcmp(name, "-") == 0;
    int* passes;
    u64 total, i;
    int ok = 1;
    char label[64];

    if (isStdout) {
        h = GetStdHandle(STD_OUTPUT_HANDLE);
        snprintf(native, sizeof native, "-");
        if (GetFileType(h) != FILE_TYPE_DISK)
            return 1;
    } else {
        if (vpath_to_native(name, native, sizeof native) != 0) {
            fprintf(stderr, PROGRAM ": %s: failed to open for writing: No such file or directory\n", name);
            return 0;
        }
        if (fs_attributes(native, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            fprintf(stderr, PROGRAM ": %s: failed to open for writing: Is a directory\n", name);
            return 0;
        }
        if (force && (attributes & FILE_ATTRIBUTE_READONLY))
            SetFileAttributesA(native, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
        h = CreateFileA(native, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            fprintf(stderr, PROGRAM ": %s: failed to open for writing: %s\n", name, fs_strerror(GetLastError()));
            return 0;
        }
    }
    if (!GetFileSizeEx(h, &fileSize))
        fileSize.QuadPart = 0;
    size = (u64)fileSize.QuadPart;
    if (sizeGiven) {
        size = sizeArg;
    } else if (!exact && size % BLOCK != 0) {
        size += BLOCK - size % BLOCK;
    }
    total = iterations + (zero ? 1 : 0);
    passes = malloc((size_t)(total + 1) * sizeof *passes);
    if (passes == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        exit(1);
    }
    make_passes(passes, iterations);
    if (zero)
        passes[iterations] = 0;
    for (i = 0; i < total && ok && size > 0; i++) {
        pattern_name(passes[i], label, sizeof label);
        if (verbose)
            fprintf(stderr, PROGRAM ": %s: pass %llu/%llu (%s)...\n", name, i + 1, total, label);
        ok = write_pass(h, size, passes[i], name);
    }
    free(passes);
    if (!isStdout)
        CloseHandle(h);
    if (ok && removeMode != REMOVE_NONE && !isStdout)
        ok = remove_file(name, native, removeMode);
    return ok;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, i, ok = 1;

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return result == ARGS_EXIT ? 0 : 1;
    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing file operand\n");
        args_usage_error(&spec);
        return 1;
    }
    if (randomSourceName != NULL) {
        char native[VPATH_MAX];

        if (vpath_to_native(randomSourceName, native, sizeof native) != 0 || (randomSource = fopen(native, "rb")) == NULL) {
            fprintf(stderr, PROGRAM ": %s: No such file or directory\n", randomSourceName);
            return 1;
        }
    }
    for (i = 0; i < count; i++)
        ok &= shred_file(operands[i]);
    return ok ? 0 : 1;
}
