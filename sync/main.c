/*
 * sync: flushes the cached writes of the files named to the disk, as GNU sync does. A file is opened and its buffers
 * are flushed (FlushFileBuffers); a directory has nothing to flush. Without FILE all the data is written by the system
 * itself: there is no call that flushes every volume without administrator rights, so it only succeeds.
 * -f (the file system of the file) opens the file and succeeds, as flushing a volume needs administrator rights.
 */

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
#define PROGRAM "sync"

static void print_usage(void) {
    puts("Usage: sync [OPTION] [FILE]...\n"
         "Synchronize cached writes to persistent storage\n"
         "\n"
         "If one or more files are specified, sync only them,\n"
         "or their containing file systems.\n"
         "\n"
         "  -d, --data             sync only file data, no unneeded metadata\n"
         "  -f, --file-system      sync the file systems that contain the files\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit");
}

enum { OPT_DATA = 1, OPT_FILE_SYSTEM };

static const ArgsOption options[] = {
    { OPT_DATA,        'd', "data",        ARGS_NONE },
    { OPT_FILE_SYSTEM, 'f', "file-system", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct { int data, fileSystem; } Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    (void)value;
    if (id == OPT_DATA)
        o->data = 1;
    else
        o->fileSystem = 1;
    return 0;
}

static int sync_file(const char* name) {
    char native[VPATH_MAX];
    HANDLE h;
    DWORD attributes;

    if (vpath_to_native(name, native, sizeof native) != 0) {
        fprintf(stderr, PROGRAM ": error opening '%s': No such file or directory\n", name);
        return 0;
    }
    h = CreateFileA(native, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) {          // a file that cannot be written to (read only, a directory): reading will do
        h = CreateFileA(native, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            fprintf(stderr, PROGRAM ": error opening '%s': %s\n", name, fs_strerror(GetLastError()));
            return 0;
        }
    }
    if (fs_attributes(native, &attributes) == 0 && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        FlushFileBuffers(h);                   // a file opened for reading only cannot be flushed: nothing to do then
    CloseHandle(h);
    return 1;
}

int main(int argc, char* argv[]) {
    Options o;
    const char** operands;
    int count, result, i, ok = 1;

    memset(&o, 0, sizeof o);
    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (o.data && o.fileSystem) {
        fprintf(stderr, PROGRAM ": cannot specify both --data and --file-system\n");
        return 1;
    }
    if (count == 0 && o.data) {
        fprintf(stderr, PROGRAM ": --data needs at least one argument\n");
        return 1;
    }
    for (i = 0; i < count; i++)
        ok &= sync_file(operands[i]);
    return ok ? 0 : 1;
}
