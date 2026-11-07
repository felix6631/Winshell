/*
 * readlink: prints the target of a symbolic link or junction. With -f, -e or -m it prints the canonical path
 * instead (the work is done by realpath, which does the same on its own: -f is its default, -e and -m are its
 * -e and -m).
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winioctl.h>
#include "args.h"
#include "cmdline.h"
#include "fsutil.h"
#include "program.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "readlink"

static void print_usage(void) {
    puts("Usage: readlink [OPTION]... FILE...\n"
         "Print value of a symbolic link or canonical file name\n"
         "\n"
         "  -f, --canonicalize            canonicalize by following every symlink in\n"
         "                                every component of the given name recursively;\n"
         "                                all but the last component must exist\n"
         "  -e, --canonicalize-existing   canonicalize by following every symlink in\n"
         "                                every component of the given name recursively,\n"
         "                                all components must exist\n"
         "  -m, --canonicalize-missing    canonicalize by following every symlink in\n"
         "                                every component of the given name recursively,\n"
         "                                without requirements on components existence\n"
         "  -n, --no-newline              do not output the trailing delimiter\n"
         "  -q, --quiet, -s, --silent     suppress most error messages (on by default)\n"
         "  -v, --verbose                 report error messages\n"
         "  -z, --zero                    end each output line with NUL, not newline\n"
         "      --help                    display this help and exit\n"
         "      --version                 output version information and exit\n"
         "\n"
         "Paths are paths of the virtual file system: C:\\Users\\me is /c/Users/me.");
}

enum { OPT_FOLLOW = 1, OPT_EXISTING, OPT_MISSING, OPT_NO_NEWLINE, OPT_QUIET, OPT_VERBOSE, OPT_ZERO };

static const ArgsOption options[] = {
    { OPT_FOLLOW,     'f', "canonicalize",            ARGS_NONE },
    { OPT_EXISTING,   'e', "canonicalize-existing",   ARGS_NONE },
    { OPT_MISSING,    'm', "canonicalize-missing",    ARGS_NONE },
    { OPT_NO_NEWLINE, 'n', "no-newline",              ARGS_NONE },
    { OPT_QUIET,      'q', "quiet",                   ARGS_NONE },
    { OPT_QUIET,      's', "silent",                  ARGS_NONE },
    { OPT_VERBOSE,    'v', "verbose",                 ARGS_NONE },
    { OPT_ZERO,       'z', "zero",                    ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int mode;           /* 0 none, OPT_FOLLOW, OPT_EXISTING, OPT_MISSING */
    int noNewline, verbose, zero;
} Settings;

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;

    (void)value;
    switch (id) {
        case OPT_FOLLOW: case OPT_EXISTING: case OPT_MISSING: s->mode = id; break;
        case OPT_NO_NEWLINE: s->noNewline = 1; break;
        case OPT_QUIET: s->verbose = 0; break;
        case OPT_VERBOSE: s->verbose = 1; break;
        case OPT_ZERO: s->zero = 1; break;
    }
    return 0;
}

typedef struct {
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union {
        struct {
            USHORT SubstituteNameOffset, SubstituteNameLength, PrintNameOffset, PrintNameLength;
            ULONG  Flags;
            WCHAR  PathBuffer[1];
        } symbolicLink;
        struct {
            USHORT SubstituteNameOffset, SubstituteNameLength, PrintNameOffset, PrintNameLength;
            WCHAR  PathBuffer[1];
        } mountPoint;
    } u;
} ReparseData;

/** The target of a link as text of the virtual file system. Returns 0, or -1 (not a link: *error is the reason). */
static int link_target(const char* native, char* out, size_t size, const char** error) {
    HANDLE file = CreateFileA(native, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    union {
        ReparseData data;
        char bytes[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    } buffer;
    DWORD got = 0;
    WCHAR* name;
    int length, relative = 0;
    char utf8[VPATH_MAX * 3];

    if (file == INVALID_HANDLE_VALUE) {
        *error = fs_strerror(GetLastError());
        return -1;
    }
    if (!DeviceIoControl(file, FSCTL_GET_REPARSE_POINT, NULL, 0, &buffer, sizeof buffer, &got, NULL)) {
        CloseHandle(file);
        *error = "Invalid argument";
        return -1;
    }
    CloseHandle(file);
    if (buffer.data.ReparseTag == IO_REPARSE_TAG_SYMLINK) {
        relative = (buffer.data.u.symbolicLink.Flags & 1) != 0;       /* SYMLINK_FLAG_RELATIVE */
        name = buffer.data.u.symbolicLink.PathBuffer +
               (relative ? buffer.data.u.symbolicLink.PrintNameOffset : buffer.data.u.symbolicLink.SubstituteNameOffset) / sizeof(WCHAR);
        length = (relative ? buffer.data.u.symbolicLink.PrintNameLength : buffer.data.u.symbolicLink.SubstituteNameLength) / (int)sizeof(WCHAR);
    } else if (buffer.data.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
        name = buffer.data.u.mountPoint.PathBuffer + buffer.data.u.mountPoint.SubstituteNameOffset / sizeof(WCHAR);
        length = buffer.data.u.mountPoint.SubstituteNameLength / (int)sizeof(WCHAR);
    } else {
        *error = "Invalid argument";
        return -1;
    }
    if (!relative && length > 4 && name[0] == L'\\' && name[1] == L'?' && name[2] == L'?' && name[3] == L'\\') {
        name += 4;
        length -= 4;
    }
    length = WideCharToMultiByte(CP_UTF8, 0, name, length, utf8, (int)sizeof utf8 - 1, NULL, NULL);
    utf8[length > 0 ? length : 0] = '\0';
    if (!relative && vpath_from_native(utf8, out, size) == 0)
        return 0;
    snprintf(out, size, "%s", utf8);
    if (relative) {
        char* p;

        for (p = out; *p != '\0'; p++)
            if (*p == '\\')
                *p = '/';
    }
    return 0;
}

/** Runs `realpath` with the arguments and returns what it printed (malloc'ed); NULL when it could not be run. */
static char* run_realpath(char* const argv[], size_t* length, int* exitCode) {
    char path[MAX_PATH];
    char* command;
    SECURITY_ATTRIBUTES inheritable = { sizeof inheritable, NULL, TRUE };
    HANDLE read = NULL, write = NULL;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char* data = NULL;
    size_t used = 0, capacity = 0;
    DWORD got, code = 1;

    if (program_find("realpath", path, sizeof path) != 0)
        return NULL;
    command = cmdline_build(argv);
    if (command == NULL || !CreatePipe(&read, &write, &inheritable, 0))
        return NULL;
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = write;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessA(path, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(read);
        CloseHandle(write);
        free(command);
        return NULL;
    }
    free(command);
    CloseHandle(write);
    for (;;) {
        char chunk[4096];
        char* grown;

        if (!ReadFile(read, chunk, sizeof chunk, &got, NULL) || got == 0)
            break;
        if (used + got + 1 > capacity) {
            capacity = (used + got + 1) * 2;
            grown = realloc(data, capacity);
            if (grown == NULL)
                break;
            data = grown;
        }
        memcpy(data + used, chunk, got);
        used += got;
    }
    CloseHandle(read);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (data == NULL)
        data = malloc(1);
    if (data != NULL)
        data[used] = '\0';
    *length = used;
    *exitCode = (int)code;
    return data;
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
    if (count > 1 && s.noNewline && s.mode == 0)
        fprintf(stderr, PROGRAM ": ignoring --no-newline with multiple arguments\n");

    for (i = 0; i < count; i++) {
        char target[VPATH_MAX * 3];
        const char* terminator = s.zero ? "\0" : "\n";
        int terminate = !(s.noNewline && count == 1);

        if (s.mode == 0) {
            char native[VPATH_MAX];
            const char* error = "Invalid argument";

            if (vpath_to_native(operands[i], native, sizeof native) != 0 || link_target(native, target, sizeof target, &error) != 0) {
                if (s.verbose)
                    fprintf(stderr, PROGRAM ": %s: %s\n", operands[i], error);
                status = 1;
                continue;
            }
            fputs(target, stdout);
        } else {
            char* arguments[6];
            char* output;
            size_t length = 0;
            int code = 0, n = 0;

            arguments[n++] = "realpath";
            if (s.mode == OPT_EXISTING)
                arguments[n++] = "-e";
            else if (s.mode == OPT_MISSING)
                arguments[n++] = "-m";
            arguments[n++] = "--";
            arguments[n++] = (char*)operands[i];
            arguments[n] = NULL;
            output = run_realpath(arguments, &length, &code);
            if (output == NULL || code != 0) {
                free(output);
                status = 1;
                continue;
            }
            while (length > 0 && (output[length - 1] == '\n' || output[length - 1] == '\r'))
                output[--length] = '\0';
            fputs(output, stdout);
            free(output);
        }
        if (terminate) {
            if (s.zero)
                fwrite(terminator, 1, 1, stdout);
            else
                fputs(terminator, stdout);
        }
    }
    return fflush(stdout) != 0 ? 1 : status;
}
