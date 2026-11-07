/*
 * uname: prints information about the system. On Windows: the system name is Windows_NT, the release is the
 * version number (10.0.26200), the version is the build, the machine is the architecture (x86_64, i686,
 * aarch64) and the node name is the name of the computer.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "uname"

static void print_usage(void) {
    puts("Usage: uname [OPTION]...\n"
         "Print certain system information.  With no OPTION, same as -s.\n"
         "\n"
         "  -a, --all                print all information, in the following order,\n"
         "                             except omit -p and -i if unknown:\n"
         "  -s, --kernel-name        print the kernel name\n"
         "  -n, --nodename           print the network node hostname\n"
         "  -r, --kernel-release     print the kernel release\n"
         "  -v, --kernel-version     print the kernel version\n"
         "  -m, --machine            print the machine hardware name\n"
         "  -p, --processor          print the processor type (non-portable)\n"
         "  -i, --hardware-platform  print the hardware platform (non-portable)\n"
         "  -o, --operating-system   print the operating system\n"
         "      --help               display this help and exit\n"
         "      --version            output version information and exit");
}

enum { OPT_ALL = 1, OPT_SYSTEM, OPT_NODE, OPT_RELEASE, OPT_VERSION, OPT_MACHINE, OPT_PROCESSOR, OPT_PLATFORM, OPT_OS };

static const ArgsOption options[] = {
    { OPT_ALL,       'a', "all",               ARGS_NONE },
    { OPT_SYSTEM,    's', "kernel-name",       ARGS_NONE },
    { OPT_NODE,      'n', "nodename",          ARGS_NONE },
    { OPT_RELEASE,   'r', "kernel-release",    ARGS_NONE },
    { OPT_VERSION,   'v', "kernel-version",    ARGS_NONE },
    { OPT_MACHINE,   'm', "machine",           ARGS_NONE },
    { OPT_PROCESSOR, 'p', "processor",         ARGS_NONE },
    { OPT_PLATFORM,  'i', "hardware-platform", ARGS_NONE },
    { OPT_OS,        'o', "operating-system",  ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int all, system, node, release, version, machine, processor, platform, os;
} Wanted;

static int on_option(int id, const char* value, void* context) {
    Wanted* w = context;

    (void)value;
    switch (id) {
        case OPT_ALL:       w->all = 1; break;
        case OPT_SYSTEM:    w->system = 1; break;
        case OPT_NODE:      w->node = 1; break;
        case OPT_RELEASE:   w->release = 1; break;
        case OPT_VERSION:   w->version = 1; break;
        case OPT_MACHINE:   w->machine = 1; break;
        case OPT_PROCESSOR: w->processor = 1; break;
        case OPT_PLATFORM:  w->platform = 1; break;
        case OPT_OS:        w->os = 1; break;
    }
    return 0;
}

typedef LONG (NTAPI* RtlGetVersionFunction)(OSVERSIONINFOW*);

static void system_version(DWORD* major, DWORD* minor, DWORD* build) {
    OSVERSIONINFOW info;
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    RtlGetVersionFunction get = ntdll != NULL ? (RtlGetVersionFunction)(void*)GetProcAddress(ntdll, "RtlGetVersion") : NULL;

    memset(&info, 0, sizeof info);
    info.dwOSVersionInfoSize = sizeof info;
    if (get == NULL || get(&info) != 0) {
        info.dwMajorVersion = 0;
        info.dwMinorVersion = 0;
        info.dwBuildNumber = 0;
    }
    *major = info.dwMajorVersion;
    *minor = info.dwMinorVersion;
    *build = info.dwBuildNumber;
}

static const char* machine_name(void) {
    SYSTEM_INFO info;

    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return "x86_64";
        case PROCESSOR_ARCHITECTURE_INTEL: return "i686";
        case PROCESSOR_ARCHITECTURE_ARM64: return "aarch64";
        case PROCESSOR_ARCHITECTURE_ARM:   return "armv7l";
        case PROCESSOR_ARCHITECTURE_IA64:  return "ia64";
        default:                           return "unknown";
    }
}

int main(int argc, char* argv[]) {
    Wanted w;
    const char** operands;
    int count, result, printed = 0;
    char node[256], release[64], version[64];
    DWORD major, minor, build, size = sizeof node;

    memset(&w, 0, sizeof w);
    _setmode(_fileno(stdout), _O_BINARY);   // a line ends with LF, as the other tools write it
    result = args_parse(&spec, argc, argv, on_option, &w, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 0) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[0]);
        args_usage_error(&spec);
        return 1;
    }
    if (w.all) {
        w.system = w.node = w.release = w.version = w.machine = w.os = 1;
        // the processor and the platform are left out when they are unknown, as they are here, unless asked for
    }
    if (!w.system && !w.node && !w.release && !w.version && !w.machine && !w.processor && !w.platform && !w.os)
        w.system = 1;

    if (!GetComputerNameExA(ComputerNameDnsHostname, node, &size) || size == 0)
        strcpy(node, "localhost");
    system_version(&major, &minor, &build);
    snprintf(release, sizeof release, "%lu.%lu.%lu", (unsigned long)major, (unsigned long)minor, (unsigned long)build);
    snprintf(version, sizeof version, "#%lu", (unsigned long)build);

#define PRINT(field) do { if (printed++) putchar(' '); fputs(field, stdout); } while (0)
    if (w.system)    PRINT("Windows_NT");
    if (w.node)      PRINT(node);
    if (w.release)   PRINT(release);
    if (w.version)   PRINT(version);
    if (w.machine)   PRINT(machine_name());
    if (w.processor && !w.all) PRINT("unknown");
    if (w.platform && !w.all)  PRINT("unknown");
    if (w.os)        PRINT("Windows");
#undef PRINT
    putchar('\n');
    return 0;
}
