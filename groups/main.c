/*
 * groups: prints the names of the groups of the current user or of the users named, as GNU groups does
 * ("user : group group" when users are named). The groups are the ones of the account (see ident.h).
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "ident.h"

#define VERSION "0.1"
#define PROGRAM "groups"

static void print_usage(void) {
    puts("Usage: groups [OPTION]... [USERNAME]...\n"
         "Print group memberships for each USERNAME or, if no USERNAME is specified, for\n"
         "the current process (which may differ if the groups database has changed).\n"
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

static Identity identity;

static int print_groups(void) {
    int i, ok = 1;

    for (i = 0; i < identity.groupCount; i++) {
        if (i > 0)
            putchar(' ');
        if (identity.groups[i].name[0] == '\0') {
            fprintf(stderr, PROGRAM ": cannot find name for group ID %u\n", identity.groups[i].id);
            printf("%u", identity.groups[i].id);
            ok = 0;
        } else {
            fputs(identity.groups[i].name, stdout);
        }
    }
    putchar('\n');
    return ok;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, i, ok = 1;

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count == 0) {
        if (ident_current(&identity) != 0) {
            fprintf(stderr, PROGRAM ": cannot find the current user\n");
            return 1;
        }
        return print_groups() ? 0 : 1;
    }
    for (i = 0; i < count; i++) {
        if (ident_user(operands[i], &identity) != 0) {
            fprintf(stderr, PROGRAM ": '%s': no such user\n", operands[i]);
            ok = 0;
            continue;
        }
        printf("%s : ", identity.user.name);
        ok &= print_groups();
    }
    return ok ? 0 : 1;
}
