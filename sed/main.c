#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "sed.h"
#include "textutil.h"
#include "vec.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "sed"

static void print_usage(void) {
    puts("Usage: sed [OPTION]... {script-only-if-no-other-script} [input-file]...\n"
         "\n"
         "  -n, --quiet, --silent     suppress automatic printing of the pattern space\n"
         "  -e script, --expression=script\n"
         "                            add the script to the commands to be executed\n"
         "  -f script-file, --file=script-file\n"
         "                            add the contents of script-file to the commands\n"
         "  -E, -r, --regexp-extended use extended regular expressions in the script\n"
         "  -i[SUFFIX], --in-place[=SUFFIX]\n"
         "                            edit files in place (makes backup if SUFFIX supplied)\n"
         "  -l N, --line-length=N     specify the desired line-wrap length for the `l' command\n"
         "  -s, --separate            consider files as separate rather than as a single\n"
         "                            continuous long stream\n"
         "  -z, --null-data           separate lines by NUL characters\n"
         "  -u, --unbuffered          flush the output after every line\n"
         "      --sandbox             reject the r, w and W commands\n"
         "      --posix, -b, --binary, --follow-symlinks\n"
         "                            (accepted, no effect)\n"
         "      --help                display this help and exit\n"
         "      --version             output version information and exit\n"
         "\n"
         "If no -e, --expression, -f, or --file option is given, then the first\n"
         "non-option argument is taken as the sed script to interpret. All remaining\n"
         "arguments are names of input files; if no input files are specified, then\n"
         "the standard input is read. A file name of - is the standard input.\n"
         "\n"
         "Not supported: the e command and the e and M flags of s.");
}

enum {
    OPT_QUIET = 1, OPT_EXPRESSION, OPT_FILE, OPT_EXTENDED, OPT_IN_PLACE, OPT_LINE_LENGTH, OPT_SEPARATE,
    OPT_NULL_DATA, OPT_UNBUFFERED, OPT_SANDBOX, OPT_ACCEPTED
};

static const ArgsOption options[] = {
    { OPT_QUIET,       'n', "quiet",            ARGS_NONE },
    { OPT_QUIET,       0,   "silent",           ARGS_NONE },
    { OPT_EXPRESSION,  'e', "expression",       ARGS_REQUIRED },
    { OPT_FILE,        'f', "file",             ARGS_REQUIRED },
    { OPT_EXTENDED,    'E', "regexp-extended",  ARGS_NONE },
    { OPT_EXTENDED,    'r', NULL,               ARGS_NONE },
    { OPT_IN_PLACE,    'i', "in-place",         ARGS_OPTIONAL },
    { OPT_LINE_LENGTH, 'l', "line-length",      ARGS_REQUIRED },
    { OPT_SEPARATE,    's', "separate",         ARGS_NONE },
    { OPT_NULL_DATA,   'z', "null-data",        ARGS_NONE },
    { OPT_UNBUFFERED,  'u', "unbuffered",       ARGS_NONE },
    { OPT_SANDBOX,     0,   "sandbox",          ARGS_NONE },
    { OPT_ACCEPTED,    'b', "binary",           ARGS_NONE },
    { OPT_ACCEPTED,    0,   "posix",            ARGS_NONE },
    { OPT_ACCEPTED,    0,   "follow-symlinks",  ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    SedConfig    config;
    ScriptChunk* chunks;
    size_t       chunkCount, chunkCapacity;
    int          expressions;
    int          failStatus;        // the exit status when an option fails: 4 for a script file that cannot be read
} Settings;

static int add_chunk(Settings* s, const char* text, size_t length, const char* file) {
    ScriptChunk* c;
    if (VEC_RESERVE(s->chunks, s->chunkCapacity, s->chunkCount + 1) != 0) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return -1;
    }
    c = &s->chunks[s->chunkCount++];
    c->text = text;
    c->length = length;
    c->file = file;
    c->number = file == NULL ? ++s->expressions : 0;
    return 0;
}

/** The content of a script file; a final line feed is dropped (it is the separator between chunks). */
static int add_script_file(Settings* s, const char* name) {
    FILE* f;
    unsigned char* text;
    size_t length;

    if (text_open(PROGRAM, name, 0, &f) != 0) {
        s->failStatus = 4;
        return -1;
    }
    text = text_slurp(f, &length);
    if (f != stdin)
        fclose(f);
    if (text == NULL) {
        fprintf(stderr, PROGRAM ": couldn't read %s\n", name);
        return -1;
    }
    if (length > 0 && text[length - 1] == '\n')
        length--;
    return add_chunk(s, (const char*)text, length, name);
}

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;

    switch (id) {
        case OPT_QUIET:       s->config.quiet = 1; break;
        case OPT_EXPRESSION:  return add_chunk(s, value, strlen(value), NULL);
        case OPT_FILE:        return add_script_file(s, value);
        case OPT_EXTENDED:    s->config.extended = 1; break;
        case OPT_IN_PLACE:
            s->config.inPlace = 1;
            s->config.separate = 1;
            s->config.suffix = value;
            break;
        case OPT_LINE_LENGTH: {
            char* end;
            long n = strtol(value, &end, 10);
            if (*value == '\0' || *end != '\0' || n < 0) {
                fprintf(stderr, PROGRAM ": invalid line length: %s\n", value);
                return -1;
            }
            s->config.lineWrap = n;
            break;
        }
        case OPT_SEPARATE:    s->config.separate = 1; break;
        case OPT_NULL_DATA:   s->config.terminator = '\0'; break;
        case OPT_UNBUFFERED:  s->config.unbuffered = 1; break;
        case OPT_SANDBOX:     s->config.sandbox = 1; break;
        default:              break;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    Settings settings;
    const char** operands;
    const char* const* files;
    const char* standardInput[1] = { "-" };
    int operandCount, result;
    size_t fileCount;
    Script script;
    char error[300];

    memset(&settings, 0, sizeof settings);
    settings.config.terminator = '\n';
    settings.config.lineWrap = 70;

    result = args_parse(&spec, argc, argv, on_option, &settings, &operands, &operandCount);
    if (result != ARGS_OK)
        return result == ARGS_FAILED && settings.failStatus != 0 ? settings.failStatus : ARGS_STATUS(spec, result);

    if (settings.chunkCount == 0) {
        if (operandCount == 0) {
            fprintf(stderr, "Usage: " PROGRAM " [OPTION]... {script-only-if-no-other-script} [input-file]...\n"
                            "Try '" PROGRAM " --help' for more information.\n");
            return 1;
        }
        if (add_chunk(&settings, operands[0], strlen(operands[0]), NULL) != 0)
            return 1;
        operands++;
        operandCount--;
    }

    result = sed_parse(settings.chunks, settings.chunkCount, &settings.config, &script, error, sizeof error);
    if (result != 0) {
        fprintf(stderr, PROGRAM ": %s\n", error);
        return result;
    }
    if (script.quiet)
        settings.config.quiet = 1;

    text_binary_streams();

    if (settings.config.inPlace && operandCount == 0) {
        fprintf(stderr, PROGRAM ": no input files\n");
        return 1;
    }
    if (operandCount == 0) {
        files = standardInput;
        fileCount = 1;
    } else {
        files = operands;
        fileCount = (size_t)operandCount;
    }
    return sed_run(&script, &settings.config, files, fileCount);
}
