/*
 * mkfifo: makes named pipes. Windows has no FIFOs in its file systems, so no file is ever made: the options and the names
 * are checked as GNU mkfifo checks them, and every name ends with the reason the file system gives ("File exists",
 * "No such file or directory", otherwise "Operation not supported"). The exit status is 1 when any name failed.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "modebits.h"
#include "specialfile.h"

#define VERSION "0.1"
#define PROGRAM "mkfifo"

static void print_usage(void) {
    puts("Usage: mkfifo [OPTION]... NAME...\n"
         "Create named pipes (FIFOs) with the given NAMEs.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  -m, --mode=MODE    set file permission bits to MODE, not a=rw - umask\n"
         "  -Z                   set the SELinux security context to default type\n"
         "      --context[=CTX]  like -Z, or if CTX is specified then set the SELinux\n"
         "                         or SMACK security context to CTX\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Windows file systems cannot hold a FIFO: every name fails with \"Operation not supported\" (or with the reason\n"
         "that applies first: the name exists, or its directory does not).");
}

enum { OPT_CONTEXT = 256 };

static const ArgsOption options[] = {
    { 'm', 'm', "mode",    ARGS_REQUIRED },
    { 'Z', 'Z', NULL,      ARGS_NONE },
    { OPT_CONTEXT, 0, "context", ARGS_OPTIONAL },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    const char* mode;
    int         context;
} Mkfifo;

static int on_option(int id, const char* value, void* context) {
    Mkfifo* m = (Mkfifo*)context;

    switch (id) {
    case 'm': m->mode = value; break;
    case OPT_CONTEXT: m->context = value != NULL; break;
    default: break;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    Mkfifo m;
    const char** operands;
    int count, result, i, status = 0;

    memset(&m, 0, sizeof m);
    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, &m, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        args_usage_error(&spec);
        return 1;
    }
    if (m.mode != NULL) {
        ModeChange* changes = mode_compile(m.mode);

        if (changes == NULL) {
            fprintf(stderr, PROGRAM ": invalid mode\n");
            return 1;
        }
        mode_free(changes);
    }
    if (m.context)
        fprintf(stderr, PROGRAM ": warning: ignoring --context; it requires an SELinux/SMACK-enabled kernel\n");
    for (i = 0; i < count; i++) {
        fprintf(stderr, PROGRAM ": cannot create fifo '%s': %s\n", operands[i], special_file_reason(operands[i]));
        status = 1;
    }
    return status;
}
