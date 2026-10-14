#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "args.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "basename"

static void print_usage(void) {
    puts("Usage: basename NAME [SUFFIX]\n"
         "  or:  basename OPTION... NAME...\n"
         "Print NAME with any leading directory components removed.\n"
         "If specified, also remove a trailing SUFFIX.\n"
         "\n"
         "  -a, --multiple       support multiple arguments and treat each as a NAME\n"
         "  -s, --suffix=SUFFIX  remove a trailing SUFFIX; implies -a\n"
         "  -z, --zero           end each output line with NUL, not newline\n"
         "      --help           display this help and exit\n"
         "      --version        output version information and exit\n"
         "\n"
         "Both / and \\ separate the components of a name.\n"
         "\n"
         "Examples:\n"
         "  basename /usr/bin/sort          -> \"sort\"\n"
         "  basename include/stdio.h .h     -> \"stdio\"\n"
         "  basename -s .h include/stdio.h  -> \"stdio\"\n"
         "  basename -a any/str1 any/str2   -> \"str1\" followed by \"str2\"");
}

enum { OPT_MULTIPLE = 1, OPT_SUFFIX, OPT_ZERO };

static const ArgsOption options[] = {
    { OPT_MULTIPLE, 'a', "multiple", ARGS_NONE },
    { OPT_SUFFIX,   's', "suffix",   ARGS_REQUIRED },
    { OPT_ZERO,     'z', "zero",     ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int         multiple;
    const char* suffix;
    int         zero;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
        case OPT_MULTIPLE: o->multiple = 1; break;
        case OPT_SUFFIX:   o->multiple = 1; o->suffix = value; break;
        case OPT_ZERO:     o->zero = 1; break;
        default:           break;
    }
    return 0;
}

/**
 * Writes the last component of name: trailing separators are not part of it, and a name of separators
 * only is "/". The suffix is removed when it is not the whole of the component.
 */
static void write_basename(const char* name, const char* suffix, int terminator) {
    size_t length = strlen(name), start, suffixLength = suffix != NULL ? strlen(suffix) : 0;

    while (length > 1 && vpath_is_separator(name[length - 1]))
        length--;
    if (length == 1 && vpath_is_separator(name[0])) {
        fputs("/", stdout);
    } else {
        start = length;
        while (start > 0 && !vpath_is_separator(name[start - 1]))
            start--;
        if (suffixLength > 0 && length - start > suffixLength &&
            memcmp(name + length - suffixLength, suffix, suffixLength) == 0)
            length -= suffixLength;
        fwrite(name + start, 1, length - start, stdout);
    }
    putchar(terminator);
}

int main(int argc, char* argv[]) {
    Options o;
    const char** operands;
    int count, result, i;
    int terminator;

    memset(&o, 0, sizeof o);
    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    if (count == 0) {
        fprintf(stderr, PROGRAM ": missing operand\nTry '" PROGRAM " --help' for more information.\n");
        return 1;
    }
    if (!o.multiple && count > 2) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\nTry '" PROGRAM " --help' for more information.\n", operands[2]);
        return 1;
    }

    _setmode(_fileno(stdout), _O_BINARY);
    terminator = o.zero ? '\0' : '\n';

    if (o.multiple) {
        for (i = 0; i < count; i++)
            write_basename(operands[i], o.suffix, terminator);
    } else {
        write_basename(operands[0], count == 2 ? operands[1] : NULL, terminator);
    }
    return fflush(stdout) != 0 ? 1 : 0;
}
