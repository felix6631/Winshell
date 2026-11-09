/*
 * id: prints the user and group information of the current user or of the users named, as GNU id does. Windows has
 * no numeric IDs: they are the relative IDs of the security identifiers (see ident.h). The real and the effective
 * ID are the same. There is no SELinux, so -Z is an error, as on a kernel without it.
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
#define PROGRAM "id"

static void print_usage(void) {
    puts("Usage: id [OPTION]... [USER]...\n"
         "Print user and group information for each specified USER,\n"
         "or (when USER omitted) for the current user.\n"
         "\n"
         "  -a             ignore, for compatibility with other versions\n"
         "  -Z, --context  print only the security context of the process\n"
         "  -g, --group    print only the effective group ID\n"
         "  -G, --groups   print all group IDs\n"
         "  -n, --name     print a name instead of a number, for -ugG\n"
         "  -r, --real     print the real ID instead of the effective ID, with -ugG\n"
         "  -u, --user     print only the effective user ID\n"
         "  -z, --zero     delimit entries with NUL characters, not whitespace;\n"
         "                   not permitted in default format\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Without any OPTION, print some useful set of identified information.");
}

enum { OPT_IGNORED = 1, OPT_CONTEXT, OPT_GROUP, OPT_GROUPS, OPT_NAME, OPT_REAL, OPT_USER, OPT_ZERO };

static const ArgsOption options[] = {
    { OPT_IGNORED, 'a', NULL,      ARGS_NONE },
    { OPT_CONTEXT, 'Z', "context", ARGS_NONE },
    { OPT_GROUP,   'g', "group",   ARGS_NONE },
    { OPT_GROUPS,  'G', "groups",  ARGS_NONE },
    { OPT_NAME,    'n', "name",    ARGS_NONE },
    { OPT_REAL,    'r', "real",    ARGS_NONE },
    { OPT_USER,    'u', "user",    ARGS_NONE },
    { OPT_ZERO,    'z', "zero",    ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int user, group, groups, name, real, zero, context;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    (void)value;
    switch (id) {
    case OPT_CONTEXT:
        fprintf(stderr, PROGRAM ": --context (-Z) works only on an SELinux-enabled kernel\n");
        return -1;
    case OPT_GROUP:  o->group = 1; break;
    case OPT_GROUPS: o->groups = 1; break;
    case OPT_NAME:   o->name = 1; break;
    case OPT_REAL:   o->real = 1; break;
    case OPT_USER:   o->user = 1; break;
    case OPT_ZERO:   o->zero = 1; break;
    default: break;
    }
    return 0;
}

static Identity identity;

/** A number, or its name with -n (a name not found stays a number and makes the status 1). */
static int print_entry(const IdentEntry* e, const Options* o, const char* kind) {
    if (o->name) {
        if (e->name[0] == '\0') {
            fprintf(stderr, PROGRAM ": cannot find name for %s ID %u\n", kind, e->id);
            printf("%u", e->id);
            return 0;
        }
        fputs(e->name, stdout);
        return 1;
    }
    printf("%u", e->id);
    return 1;
}

static int show(const Options* o, int defaultFormat) {
    int ok = 1, i;
    char end = o->zero ? '\0' : '\n';

    if (o->user) {
        ok &= print_entry(&identity.user, o, "user");
        putchar(end);
    } else if (o->group) {
        ok &= print_entry(&identity.primary, o, "group");
        putchar(end);
    } else if (o->groups) {
        for (i = 0; i < identity.groupCount; i++) {
            if (i > 0)
                putchar(o->zero ? '\0' : ' ');
            ok &= print_entry(&identity.groups[i], o, "group");
        }
        putchar(end);
    } else if (defaultFormat) {
        printf("uid=%u", identity.user.id);
        if (identity.user.name[0] != '\0')
            printf("(%s)", identity.user.name);
        printf(" gid=%u", identity.primary.id);
        if (identity.primary.name[0] != '\0')
            printf("(%s)", identity.primary.name);
        if (identity.groupCount > 0) {
            fputs(" groups=", stdout);
            for (i = 0; i < identity.groupCount; i++) {
                if (i > 0)
                    putchar(',');
                printf("%u", identity.groups[i].id);
                if (identity.groups[i].name[0] != '\0')
                    printf("(%s)", identity.groups[i].name);
            }
        }
        putchar('\n');
    }
    return ok;
}

int main(int argc, char* argv[]) {
    Options o;
    const char** operands;
    int count, result, i, ok = 1, defaultFormat, choices;

    memset(&o, 0, sizeof o);
    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &count);
    if (result != ARGS_OK)
        return result == ARGS_EXIT ? 0 : 1;
    choices = o.user + o.group + o.groups;
    defaultFormat = choices == 0;
    if (choices > 1) {
        fprintf(stderr, PROGRAM ": cannot print \"only\" of more than one choice\n");
        return 1;
    }
    if (defaultFormat && (o.name || o.real)) {
        fprintf(stderr, PROGRAM ": cannot print only names or real IDs in default format\n");
        return 1;
    }
    if (defaultFormat && o.zero) {
        fprintf(stderr, PROGRAM ": option --zero not permitted in default format\n");
        return 1;
    }
    if (count == 0) {
        if (ident_current(&identity) != 0) {
            fprintf(stderr, PROGRAM ": cannot find the current user\n");
            return 1;
        }
        return show(&o, defaultFormat) ? 0 : 1;
    }
    for (i = 0; i < count; i++) {
        if (ident_user(operands[i], &identity) != 0) {
            fprintf(stderr, PROGRAM ": '%s': no such user\n", operands[i]);
            ok = 0;
            continue;
        }
        ok &= show(&o, defaultFormat);
    }
    return ok ? 0 : 1;
}
