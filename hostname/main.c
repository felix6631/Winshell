/*
 * hostname: prints the name of the computer. Setting it needs the rights of an administrator and a restart on
 * Windows, so `hostname NAME` is refused.
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"

#define VERSION "0.1"
#define PROGRAM "hostname"

static void print_usage(void) {
    puts("Usage: hostname [OPTION]...\n"
         "Print the name of the computer.\n"
         "\n"
         "  -s, --short   the name up to the first dot\n"
         "  -f, --fqdn    the fully qualified domain name (also --long)\n"
         "  -d, --domain  the DNS domain name\n"
         "      --help    display this help and exit\n"
         "      --version output version information and exit");
}

enum { OPT_SHORT = 1, OPT_FQDN, OPT_DOMAIN };

static const ArgsOption options[] = {
    { OPT_SHORT,  's', "short",  ARGS_NONE },
    { OPT_FQDN,   'f', "fqdn",   ARGS_NONE },
    { OPT_FQDN,   0,   "long",   ARGS_NONE },
    { OPT_DOMAIN, 'd', "domain", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    (void)value;
    *(int*)context = id;
    return 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, mode = 0;
    char name[512];
    DWORD size = sizeof name;
    COMPUTER_NAME_FORMAT format = ComputerNameDnsHostname;
    _setmode(_fileno(stdout), _O_BINARY);   // a line ends with LF, as the other tools write it

    result = args_parse(&spec, argc, argv, on_option, &mode, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 1) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[1]);
        args_usage_error(&spec);
        return 1;
    }
    if (count == 1) {
        fprintf(stderr, PROGRAM ": you must be root to change the host name\n");
        return 1;
    }

    if (mode == OPT_FQDN)
        format = ComputerNameDnsFullyQualified;
    else if (mode == OPT_DOMAIN)
        format = ComputerNameDnsDomain;
    if (!GetComputerNameExA(format, name, &size)) {
        fprintf(stderr, PROGRAM ": cannot determine the host name\n");
        return 1;
    }
    if (mode == OPT_SHORT) {
        char* dot = strchr(name, '.');

        if (dot != NULL)
            *dot = '\0';
    }
    printf("%s\n", name);
    return 0;
}
