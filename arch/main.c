/*
 * arch: prints the architecture of the machine, as GNU arch (uname -m) does: x86_64, i686, aarch64, ...
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <windows.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "arch"

static void print_usage(void) {
    puts("Usage: arch [OPTION]...\n"
         "Print machine architecture.\n"
         "\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit");
}

static const ArgsOption options[] = { { 0 } };
static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    (void)id;
    (void)value;
    (void)context;
    return 0;
}

/** The name of the architecture the way Linux spells it. */
static const char* machine(void) {
    SYSTEM_INFO info;

    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_INTEL: return "i686";
    case PROCESSOR_ARCHITECTURE_AMD64:
    case PROCESSOR_ARCHITECTURE_IA32_ON_WIN64: return "x86_64";
    case PROCESSOR_ARCHITECTURE_ARM64:
    case PROCESSOR_ARCHITECTURE_IA32_ON_ARM64: return "aarch64";
    case PROCESSOR_ARCHITECTURE_ARM:
    case PROCESSOR_ARCHITECTURE_ARM32_ON_WIN64: return "armv7l";
    case PROCESSOR_ARCHITECTURE_IA64: return "ia64";
    case PROCESSOR_ARCHITECTURE_MIPS: return "mips";
    case PROCESSOR_ARCHITECTURE_ALPHA:
    case PROCESSOR_ARCHITECTURE_ALPHA64: return "alpha";
    case PROCESSOR_ARCHITECTURE_PPC: return "powerpc";
    case PROCESSOR_ARCHITECTURE_SHX: return "sh4";
    case PROCESSOR_ARCHITECTURE_MSIL:
    case PROCESSOR_ARCHITECTURE_NEUTRAL: return "anycpu";
    default: return "unknown";
    }
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result;

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 0) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[0]);
        args_usage_error(&spec);
        return 1;
    }
    printf("%s\n", machine());
    return 0;
}
