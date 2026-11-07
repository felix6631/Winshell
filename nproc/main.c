/*
 * nproc: prints the number of processors the process may use (its affinity), or all of them with --all.
 * OMP_NUM_THREADS and OMP_THREAD_LIMIT are honoured, as GNU nproc does.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "nproc"

static void print_usage(void) {
    puts("Usage: nproc [OPTION]...\n"
         "Print the number of processing units available to the current process,\n"
         "which may be less than the number of online processors.\n"
         "\n"
         "      --all      print the number of installed processors\n"
         "      --ignore=N  if possible, exclude N processing units\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit");
}

enum { OPT_ALL = 1, OPT_IGNORE };

static const ArgsOption options[] = {
    { OPT_ALL,    0, "all",    ARGS_NONE },
    { OPT_IGNORE, 0, "ignore", ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int  all;
    long ignore;
} Settings;

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;

    if (id == OPT_ALL) {
        s->all = 1;
    } else {
        char* end;

        s->ignore = strtol(value, &end, 10);
        if (*value == '\0' || *end != '\0' || s->ignore < 0) {
            fprintf(stderr, PROGRAM ": invalid number: '%s'\n", value);
            return -1;
        }
    }
    return 0;
}

/** The value of an OpenMP variable (the first number of a list), or 0 when it is not a positive number. */
static unsigned long omp_value(const char* name) {
    const char* text = getenv(name);
    char* end;
    unsigned long value;

    if (text == NULL)
        return 0;
    while (isspace((unsigned char)*text))
        text++;
    value = strtoul(text, &end, 10);
    if (end == text || (*end != '\0' && *end != ',' && !isspace((unsigned char)*end)))
        return 0;
    return value;
}

int main(int argc, char* argv[]) {
    Settings settings;
    const char** operands;
    int count, result;
    unsigned long total;

    memset(&settings, 0, sizeof settings);
    _setmode(_fileno(stdout), _O_BINARY);   // a line ends with LF, as the other tools write it
    result = args_parse(&spec, argc, argv, on_option, &settings, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 0) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[0]);
        args_usage_error(&spec);
        return 1;
    }

    if (settings.all) {
        total = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    } else {
        DWORD_PTR process = 0, system = 0;
        unsigned long limit;

        total = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system) && process != 0) {
            DWORD_PTR bits = process;

            while (bits != 0) {
                total += (unsigned long)(bits & 1);
                bits >>= 1;
            }
            // a machine with several processor groups has more than the 64 of the mask
            if (total == 0 || (system != 0 && process == system && GetActiveProcessorGroupCount() > 1))
                total = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        } else {
            total = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        }
        limit = omp_value("OMP_NUM_THREADS");
        if (limit > 0)
            total = limit;
        limit = omp_value("OMP_THREAD_LIMIT");
        if (limit > 0 && limit < total)
            total = limit;
    }
    if (total == 0)
        total = 1;
    total = (unsigned long)settings.ignore < total ? total - (unsigned long)settings.ignore : 1;
    printf("%lu\n", total);
    return 0;
}
