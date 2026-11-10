/*
 * mknod: makes special files (block and character devices, FIFOs). Windows file systems hold none of them, so no file is ever
 * made: the options and the operands are checked as GNU mknod checks them (the TYPE, the device numbers that b, c and u need
 * and p must not have), and the name ends with the reason the file system gives ("File exists", "No such file or
 * directory", otherwise "Operation not supported").
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
#include "modebits.h"
#include "specialfile.h"

#define VERSION "0.1"
#define PROGRAM "mknod"

static void print_usage(void) {
    puts("Usage: mknod [OPTION]... NAME TYPE [MAJOR MINOR]\n"
         "Create the special file NAME of the given TYPE.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  -m, --mode=MODE    set file permission bits to MODE, not a=rw - umask\n"
         "  -Z                   set the SELinux security context to default type\n"
         "      --context[=CTX]  like -Z, or if CTX is specified then set the SELinux\n"
         "                         or SMACK security context to CTX\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Both MAJOR and MINOR must be specified when TYPE is b, c, or u, and they\n"
         "must be omitted when TYPE is p.  If MAJOR or MINOR begins with 0x or 0X,\n"
         "it is interpreted as hexadecimal; otherwise, if it begins with 0, as octal;\n"
         "otherwise, as decimal.  TYPE may be:\n"
         "\n"
         "  b      create a block (buffered) special file\n"
         "  c, u   create a character (unbuffered) special file\n"
         "  p      create a FIFO\n"
         "\n"
         "Windows file systems cannot hold such files: the name fails with \"Operation not supported\" (or with the reason\n"
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
} Mknod;

static int on_option(int id, const char* value, void* context) {
    Mknod* m = (Mknod*)context;

    switch (id) {
    case 'm': m->mode = value; break;
    case OPT_CONTEXT: m->context = value != NULL; break;
    default: break;
    }
    return 0;
}

/** A device number: decimal, octal with a 0 and hexadecimal with 0x. Returns 0, or -1 (not a number, or too large). */
static int parse_device(const char* text, unsigned long long* out) {
    char* end;

    if (*text == '\0' || isspace((unsigned char)*text) || *text == '-' || *text == '+')
        return -1;
    errno = 0;
    *out = strtoull(text, &end, 0);
    if (*end != '\0' || errno != 0)
        return -1;
    return 0;
}

int main(int argc, char* argv[]) {
    Mknod m;
    const char** operands;
    int count, result, isFifo;
    unsigned long long major, minor;
    const char* type;

    memset(&m, 0, sizeof m);
    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, &m, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (m.mode != NULL) {
        ModeChange* changes = mode_compile(m.mode);

        if (changes == NULL) {
            fprintf(stderr, PROGRAM ": invalid mode\n");
            return 1;
        }
        mode_free(changes);
    }
    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing operand\n");
        args_usage_error(&spec);
        return 1;
    }
    if (count == 1) {
        fprintf(stderr, PROGRAM ": missing operand after '%s'\n", operands[0]);
        args_usage_error(&spec);
        return 1;
    }
    type = operands[1];
    isFifo = strcmp(type, "p") == 0;
    if (isFifo) {
        if (count > 2) {
            fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[2]);
            if (count == 4)
                fprintf(stderr, "Fifos do not have major and minor device numbers.\n");
            args_usage_error(&spec);
            return 1;
        }
    } else {
        if (count < 4) {
            fprintf(stderr, PROGRAM ": missing operand after '%s'\n", operands[count - 1]);
            if (count == 2)
                fprintf(stderr, "Special files require major and minor device numbers.\n");
            args_usage_error(&spec);
            return 1;
        }
        if (count > 4) {
            fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[4]);
            args_usage_error(&spec);
            return 1;
        }
        if (strcmp(type, "b") != 0 && strcmp(type, "c") != 0 && strcmp(type, "u") != 0) {
            fprintf(stderr, PROGRAM ": invalid device type '%s'\n", type);
            args_usage_error(&spec);
            return 1;
        }
        if (parse_device(operands[2], &major) != 0) {
            fprintf(stderr, PROGRAM ": invalid major device number '%s'\n", operands[2]);
            return 1;
        }
        if (parse_device(operands[3], &minor) != 0) {
            fprintf(stderr, PROGRAM ": invalid minor device number '%s'\n", operands[3]);
            return 1;
        }
    }
    if (m.context)
        fprintf(stderr, PROGRAM ": warning: ignoring --context; it requires an SELinux/SMACK-enabled kernel\n");
    fprintf(stderr, PROGRAM ": %s: %s\n", operands[0], special_file_reason(operands[0]));
    return 1;
}
