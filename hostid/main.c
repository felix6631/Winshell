/*
 * hostid: prints the identifier of the host as eight hexadecimal digits. Windows has no gethostid: the number is made
 * from the machine GUID of the installation (the computer name when there is none), so it stays the same on a host.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "hostid"

static void print_usage(void) {
    puts("Usage: hostid [OPTION]\n"
         "Print the numeric identifier (in hexadecimal) for the current host.\n"
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

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result;
    char text[256];
    DWORD size = sizeof text;
    unsigned hash = 2166136261u;
    const unsigned char* p;

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 0) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[0]);
        args_usage_error(&spec);
        return 1;
    }
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", "MachineGuid", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                     NULL, text, &size) != ERROR_SUCCESS) {
        size = sizeof text;
        if (!GetComputerNameA(text, &size))
            text[0] = '\0';
    }
    for (p = (const unsigned char*)text; *p != '\0'; p++)
        hash = (hash ^ *p) * 16777619u;
    printf("%08x\n", hash);
    return 0;
}
