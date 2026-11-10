/*
 * ownership: the shared part of chown and chgrp.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "ident.h"
#include "ownership.h"
#include "vpath.h"

#define VERSION "0.1"

static const char* program;
static int isGroupTool;

static void print_usage_chown(void) {
    puts("Usage: chown [OPTION]... [OWNER][:[GROUP]] FILE...\n"
         "  or:  chown [OPTION]... --reference=RFILE FILE...\n"
         "Change the owner and/or group of each FILE to OWNER and/or GROUP.\n"
         "With --reference, change the owner and group of each FILE to those of RFILE.\n"
         "\n"
         "  -c, --changes          like verbose but report only when a change is made\n"
         "  -f, --silent, --quiet  suppress most error messages\n"
         "  -v, --verbose          output a diagnostic for every file processed\n"
         "      --dereference      affect the referent of each symbolic link (this is\n"
         "                         the default), rather than the symbolic link itself\n"
         "  -h, --no-dereference   affect symbolic links instead of any referenced file\n"
         "                         (useful only on systems that can change the\n"
         "                         ownership of a symlink)\n"
         "      --from=CURRENT_OWNER:CURRENT_GROUP\n"
         "                         change the owner and/or group of each file only if\n"
         "                         its current owner and/or group match those specified\n"
         "                         here.  Either may be omitted, in which case a match\n"
         "                         is not required for the omitted attribute\n"
         "      --no-preserve-root  do not treat '/' specially (the default)\n"
         "      --preserve-root    fail to operate recursively on '/'\n"
         "      --reference=RFILE  use RFILE's owner and group rather than\n"
         "                         specifying OWNER:GROUP values\n"
         "  -R, --recursive        operate on files and directories recursively\n"
         "\n"
         "The following options modify how a hierarchy is traversed when the -R\n"
         "option is also specified.  If more than one is specified, only the final\n"
         "one takes effect.\n"
         "\n"
         "  -H                     if a command line argument is a symbolic link\n"
         "                         to a directory, traverse it\n"
         "  -L                     traverse every symbolic link to a directory\n"
         "                         encountered\n"
         "  -P                     do not traverse any symbolic links (default)\n"
         "\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Owner is unchanged if missing.  Group is unchanged if missing, but changed\n"
         "to login group if implied by a ':' following a symbolic OWNER.\n"
         "OWNER and GROUP may be numeric as well as symbolic.\n"
         "\n"
         "Examples:\n"
         "  chown root /u        Change the owner of /u to \"root\".\n"
         "  chown root:staff /u  Likewise, but also change its group to \"staff\".\n"
         "  chown -hR root /u    Change the owner of /u and subfiles to \"root\".");
}

static void print_usage_chgrp(void) {
    puts("Usage: chgrp [OPTION]... GROUP FILE...\n"
         "  or:  chgrp [OPTION]... --reference=RFILE FILE...\n"
         "Change the group of each FILE to GROUP.\n"
         "With --reference, change the group of each FILE to that of RFILE.\n"
         "\n"
         "  -c, --changes          like verbose but report only when a change is made\n"
         "  -f, --silent, --quiet  suppress most error messages\n"
         "  -v, --verbose          output a diagnostic for every file processed\n"
         "      --dereference      affect the referent of each symbolic link (this is\n"
         "                         the default), rather than the symbolic link itself\n"
         "  -h, --no-dereference   affect symbolic links instead of any referenced file\n"
         "                         (useful only on systems that can change the\n"
         "                         ownership of a symlink)\n"
         "      --no-preserve-root  do not treat '/' specially (the default)\n"
         "      --preserve-root    fail to operate recursively on '/'\n"
         "      --reference=RFILE  use RFILE's group rather than specifying a\n"
         "                         GROUP value\n"
         "  -R, --recursive        operate on files and directories recursively\n"
         "\n"
         "The following options modify how a hierarchy is traversed when the -R\n"
         "option is also specified.  If more than one is specified, only the final\n"
         "one takes effect.\n"
         "\n"
         "  -H                     if a command line argument is a symbolic link\n"
         "                         to a directory, traverse it\n"
         "  -L                     traverse every symbolic link to a directory\n"
         "                         encountered\n"
         "  -P                     do not traverse any symbolic links (default)\n"
         "\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Examples:\n"
         "  chgrp staff /u      Change the group of /u to \"staff\".\n"
         "  chgrp -hR staff /u  Change the group of /u and subfiles to \"staff\".");
}

static void usage_dispatch(void) {
    if (isGroupTool)
        print_usage_chgrp();
    else
        print_usage_chown();
}

enum {
    OPT_CHANGES = 1, OPT_SILENT, OPT_VERBOSE, OPT_DEREFERENCE, OPT_NO_DEREFERENCE, OPT_FROM, OPT_NO_PRESERVE, OPT_PRESERVE,
    OPT_REFERENCE, OPT_RECURSIVE, OPT_H, OPT_L, OPT_P
};

static const ArgsOption options[] = {
    { OPT_CHANGES,        'c', "changes",          ARGS_NONE },
    { OPT_SILENT,         'f', "silent",           ARGS_NONE },
    { OPT_SILENT,         0,   "quiet",            ARGS_NONE },
    { OPT_VERBOSE,        'v', "verbose",          ARGS_NONE },
    { OPT_DEREFERENCE,    0,   "dereference",      ARGS_NONE },
    { OPT_NO_DEREFERENCE, 'h', "no-dereference",   ARGS_NONE },
    { OPT_FROM,           0,   "from",             ARGS_REQUIRED },
    { OPT_NO_PRESERVE,    0,   "no-preserve-root", ARGS_NONE },
    { OPT_PRESERVE,       0,   "preserve-root",    ARGS_NONE },
    { OPT_REFERENCE,      0,   "reference",        ARGS_REQUIRED },
    { OPT_RECURSIVE,      'R', "recursive",        ARGS_NONE },
    { OPT_H,              'H', NULL,               ARGS_NONE },
    { OPT_L,              'L', NULL,               ARGS_NONE },
    { OPT_P,              'P', NULL,               ARGS_NONE },
    { 0 }
};

static ArgsSpec spec;

static int changesOnly, silent, verbose, recursive, preserveRoot, noDereference;
static char traverse = 'P';
static const char* fromSpec;
static const char* referenceFile;

static int on_option(int id, const char* value, void* context) {
    (void)context;
    switch (id) {
    case OPT_CHANGES: changesOnly = 1; verbose = 0; break;
    case OPT_SILENT: silent = 1; break;
    case OPT_VERBOSE: verbose = 1; changesOnly = 0; break;
    case OPT_DEREFERENCE: noDereference = 0; break;
    case OPT_NO_DEREFERENCE: noDereference = 1; break;
    case OPT_FROM: fromSpec = value; break;
    case OPT_NO_PRESERVE: preserveRoot = 0; break;
    case OPT_PRESERVE: preserveRoot = 1; break;
    case OPT_REFERENCE: referenceFile = value; break;
    case OPT_RECURSIVE: recursive = 1; break;
    case OPT_H: traverse = 'H'; break;
    case OPT_L: traverse = 'L'; break;
    case OPT_P: traverse = 'P'; break;
    default: break;
    }
    return 0;
}

static const char* quote(const char* text) {
    static char buffer[4][2048];
    static int which;
    char* out = buffer[which++ & 3];
    size_t n = 0;
    const unsigned char* p;

    out[n++] = '\'';
    for (p = (const unsigned char*)text; *p != '\0' && n < 2048 - 8; p++) {
        if (*p == '\'') {
            out[n++] = '\'';
            out[n++] = '\\';
            out[n++] = '\'';
            out[n++] = '\'';
        } else {
            out[n++] = (char)*p;
        }
    }
    out[n++] = '\'';
    out[n] = '\0';
    return out;
}

/* ---- owners and groups ---- */

static Identity current;

typedef struct {
    int      haveUser, haveGroup;
    unsigned user, group;
    char     userName[256], groupName[256];
} Spec;

static int all_digits(const char* s) {
    if (*s == '\0')
        return 0;
    for (; *s != '\0'; s++) {
        if (!isdigit((unsigned char)*s))
            return 0;
    }
    return 1;
}

/** A user by name or number. Returns 0, or -1 when there is none. */
static int find_user(const char* name, unsigned* id, char* shown, size_t size) {
    Identity other;

    if (ident_user(name, &other) == 0) {
        *id = other.user.id;
        snprintf(shown, size, "%s", other.user.name);
        return 0;
    }
    if (all_digits(name)) {
        *id = (unsigned)strtoul(name, NULL, 10);
        if (*id == current.user.id)
            snprintf(shown, size, "%s", current.user.name);
        else
            snprintf(shown, size, "%u", *id);
        return 0;
    }
    return -1;
}

static int find_group(const char* name, unsigned* id, char* shown, size_t size) {
    if (current.primary.name[0] != '\0' && _stricmp(name, current.primary.name) == 0) {
        *id = current.primary.id;
        snprintf(shown, size, "%s", current.primary.name);
        return 0;
    }
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof sid, domainSize = 256;
    char domain[256];
    SID_NAME_USE use;

    if (LookupAccountNameA(NULL, name, (PSID)sid, &sidSize, domain, &domainSize, &use) &&
        (use == SidTypeGroup || use == SidTypeAlias || use == SidTypeWellKnownGroup)) {
        UCHAR count = *GetSidSubAuthorityCount((PSID)sid);
        char canonical[256];
        DWORD canonicalSize = sizeof canonical;

        *id = count == 0 ? 0 : (unsigned)*GetSidSubAuthority((PSID)sid, (DWORD)(count - 1));
        domainSize = sizeof domain;
        if (LookupAccountSidA(NULL, (PSID)sid, canonical, &canonicalSize, domain, &domainSize, &use))
            snprintf(shown, size, "%s", canonical);
        else
            snprintf(shown, size, "%s", name);
        return 0;
    }
    if (all_digits(name)) {
        *id = (unsigned)strtoul(name, NULL, 10);
        if (*id == current.primary.id)
            snprintf(shown, size, "%s", current.primary.name[0] != '\0' ? current.primary.name : name);
        else
            snprintf(shown, size, "%u", *id);
        return 0;
    }
    return -1;
}

/** OWNER, OWNER:GROUP, OWNER., :GROUP, : and the like. Returns 0, or -1 after a message. */
static int parse_spec(const char* text, Spec* out, int groupOnly) {
    char buffer[512];
    char* colon;
    const char* userPart;
    const char* groupPart = NULL;

    memset(out, 0, sizeof *out);
    snprintf(buffer, sizeof buffer, "%s", text);
    if (groupOnly) {
        groupPart = buffer;
        userPart = NULL;
    } else {
        Identity probe;

        colon = strchr(buffer, ':');
        if (colon == NULL && ident_user(buffer, &probe) != 0 && strchr(buffer, '.') != NULL && !all_digits(buffer)) {
            // the old OWNER.GROUP form, when no user has a name with a dot
            colon = strchr(buffer, '.');
        }
        if (colon != NULL) {
            *colon = '\0';
            groupPart = colon + 1;
        }
        userPart = buffer;
    }
    if (userPart != NULL && *userPart != '\0') {
        if (find_user(userPart, &out->user, out->userName, sizeof out->userName) != 0) {
            fprintf(stderr, "%s: invalid user: %s\n", program, quote(text));
            return -1;
        }
        out->haveUser = 1;
    }
    if (groupPart != NULL && *groupPart != '\0') {
        if (find_group(groupPart, &out->group, out->groupName, sizeof out->groupName) != 0) {
            fprintf(stderr, "%s: invalid group: %s\n", program, quote(text));
            return -1;
        }
        out->haveGroup = 1;
    } else if (groupPart != NULL && userPart != NULL && *userPart != '\0') {
        // OWNER: means the login group of the owner
        out->haveGroup = 1;
        out->group = current.primary.id;
        snprintf(out->groupName, sizeof out->groupName, "%s",
                 current.primary.name[0] != '\0' ? current.primary.name : "");
        if (out->groupName[0] == '\0')
            snprintf(out->groupName, sizeof out->groupName, "%u", out->group);
    }
    return 0;
}

static Spec wanted, fromWanted;
static int haveFrom;
static int ok = 1;

static void owner_text(char* out, size_t size, const char* user, const char* group, int haveUser, int haveGroup) {
    if (haveUser && haveGroup)
        snprintf(out, size, "%s:%s", user, group);
    else if (haveUser)
        snprintf(out, size, "%s", user);
    else if (haveGroup)
        snprintf(out, size, "%s", group);
    else
        out[0] = '\0';
}

static const char* current_group_name(char* buffer, size_t size) {
    if (current.primary.name[0] != '\0')
        snprintf(buffer, size, "%s", current.primary.name);
    else
        snprintf(buffer, size, "%u", current.primary.id);
    return buffer;
}

static void describe(const char* name, int failed, int changed, const Spec* target) {
    char groupNow[256], newText[512], oldText[512];
    int haveUser = target->haveUser, haveGroup = target->haveGroup;

    current_group_name(groupNow, sizeof groupNow);
    owner_text(newText, sizeof newText, target->userName, target->groupName, haveUser, haveGroup);
    owner_text(oldText, sizeof oldText, current.user.name, groupNow, haveUser, haveGroup);
    if (failed) {
        if (haveUser || haveGroup) {
            if (isGroupTool || (!haveUser && haveGroup))
                printf("failed to change group of %s from %s to %s\n", quote(name), oldText, newText);
            else
                printf("failed to change ownership of %s from %s to %s\n", quote(name), oldText, newText);
        } else {
            printf("failed to change ownership of %s\n", quote(name));
        }
        return;
    }
    if (changed) {
        if (isGroupTool || (!haveUser && haveGroup))
            printf("changed group of %s from %s to %s\n", quote(name), oldText, newText);
        else
            printf("changed ownership of %s from %s to %s\n", quote(name), oldText, newText);
        return;
    }
    if (haveUser || haveGroup) {
        if (isGroupTool || (!haveUser && haveGroup))
            printf("group of %s retained as %s\n", quote(name), newText);
        else
            printf("ownership of %s retained as %s\n", quote(name), newText);
    } else {
        printf("ownership of %s retained\n", quote(name));
    }
}

typedef struct {
    DWORD attributes;
    int   isLink;
    int   isDirectory;
} Info;

static int read_info(const char* native, int follow, Info* info, DWORD* error) {
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    HANDLE h;
    BY_HANDLE_FILE_INFORMATION data;

    if (!follow)
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    h = CreateFileA(native, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, flags, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return -1;
    }
    if (!GetFileInformationByHandle(h, &data)) {
        *error = GetLastError();
        CloseHandle(h);
        return -1;
    }
    CloseHandle(h);
    info->attributes = data.dwFileAttributes;
    info->isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info->isLink = !follow && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    return 0;
}

static void process(const char* name, const char* native, int commandLine);

static void process_children(const char* name, const char* native) {
    char pattern[VPATH_MAX];
    WIN32_FIND_DATAA data;
    HANDLE find;

    snprintf(pattern, sizeof pattern, "%s\\*", native);
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        char childName[VPATH_MAX], childNative[VPATH_MAX];

        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
            continue;
        fs_join(name, data.cFileName, childName, sizeof childName);
        fs_join(native, data.cFileName, childNative, sizeof childNative);
        process(childName, childNative, 0);
    } while (FindNextFileA(find, &data));
    FindClose(find);
}

static void process(const char* name, const char* native, int commandLine) {
    Info info, target;
    DWORD error = 0;
    int follow = !noDereference;
    int changeNeeded;
    int enter;

    if (read_info(native, follow, &info, &error) != 0) {
        if (!silent)
            fprintf(stderr, "%s: cannot access %s: %s\n", program, quote(name), fs_strerror(error));
        ok = 0;
        if (verbose) {
            char newText[512];

            owner_text(newText, sizeof newText, wanted.userName, wanted.groupName, wanted.haveUser, wanted.haveGroup);
            if (newText[0] != '\0')
                printf("failed to change ownership of %s to %s\n", quote(name), newText);
            else
                printf("failed to change ownership of %s\n", quote(name));
        }
        return;
    }
    // the directory the traversal goes into: a link only by -H (on the command line) or -L
    enter = recursive && info.isDirectory;
    if (recursive && !info.isDirectory) {
        enter = 0;
    }
    if (recursive && !commandLine && traverse != 'L') {
        Info own;

        if (read_info(native, 0, &own, &error) == 0 && (own.attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            enter = 0;
    }
    if (recursive && commandLine && traverse == 'P') {
        Info own;

        if (read_info(native, 0, &own, &error) == 0 && (own.attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            enter = 0;
    }
    (void)target;
    // --from: only the files whose owner and group are those
    if (haveFrom) {
        if ((fromWanted.haveUser && fromWanted.user != current.user.id) || (fromWanted.haveGroup && fromWanted.group != current.primary.id)) {
            if (enter)
                process_children(name, native);
            return;
        }
    }
    changeNeeded = (wanted.haveUser && wanted.user != current.user.id) || (wanted.haveGroup && wanted.group != current.primary.id);
    if (changeNeeded) {
        if (!silent) {
            if (isGroupTool || (!wanted.haveUser && wanted.haveGroup))
                fprintf(stderr, "%s: changing group of %s: Operation not permitted\n", program, quote(name));
            else
                fprintf(stderr, "%s: changing ownership of %s: Operation not permitted\n", program, quote(name));
        }
        ok = 0;
        if (verbose || changesOnly)
            describe(name, 1, 0, &wanted);
    } else if (verbose) {
        describe(name, 0, 0, &wanted);
    }
    if (enter)
        process_children(name, native);
}

int ownership_main(int argc, char* argv[], int isGroup) {
    const char** operands;
    int count, result, i, first = 0;
    char native[VPATH_MAX];

    isGroupTool = isGroup;
    program = isGroup ? "chgrp" : "chown";
    memset(&spec, 0, sizeof spec);
    spec.program = program;
    spec.version = VERSION;
    spec.usage = usage_dispatch;
    spec.options = options;
    spec.usageStatus = 1;
    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (ident_current(&current) != 0) {
        fprintf(stderr, "%s: cannot find the current user\n", program);
        return 1;
    }
    if (referenceFile == NULL) {
        if (count == 0) {
            fprintf(stderr, "%s: missing operand\n", program);
            args_usage_error(&spec);
            return 1;
        }
        first = 1;
        if (count == 1) {
            fprintf(stderr, "%s: missing operand after %s\n", program, quote(operands[0]));
            args_usage_error(&spec);
            return 1;
        }
        if (parse_spec(operands[0], &wanted, isGroup) != 0)
            return 1;
    } else {
        // the owner and the group of the reference file: those of the current user
        Info info;
        DWORD error = 0;

        if (count == 0) {
            fprintf(stderr, "%s: missing operand\n", program);
            args_usage_error(&spec);
            return 1;
        }
        if (vpath_to_native(referenceFile, native, sizeof native) != 0 || read_info(native, 1, &info, &error) != 0) {
            fprintf(stderr, "%s: failed to get attributes of %s: %s\n", program, quote(referenceFile),
                    fs_strerror(error != 0 ? error : ERROR_FILE_NOT_FOUND));
            return 1;
        }
        wanted.haveGroup = 1;
        wanted.group = current.primary.id;
        snprintf(wanted.groupName, sizeof wanted.groupName, "%s", current.primary.name[0] != '\0' ? current.primary.name : "0");
        if (!isGroup) {
            wanted.haveUser = 1;
            wanted.user = current.user.id;
            snprintf(wanted.userName, sizeof wanted.userName, "%s", current.user.name);
        }
    }
    if (fromSpec != NULL && !isGroup) {
        if (parse_spec(fromSpec, &fromWanted, 0) != 0)
            return 1;
        haveFrom = 1;
    }
    for (i = first; i < count; i++) {
        if (preserveRoot && recursive && strcmp(operands[i], "/") == 0) {
            fprintf(stderr, "%s: it is dangerous to operate recursively on '/'\n", program);
            fprintf(stderr, "%s: use --no-preserve-root to override this failsafe\n", program);
            ok = 0;
            continue;
        }
        if (vpath_to_native(operands[i], native, sizeof native) != 0) {
            if (!silent)
                fprintf(stderr, "%s: cannot access %s: No such file or directory\n", program, quote(operands[i]));
            ok = 0;
            if (verbose) {
                char newText[512];

                owner_text(newText, sizeof newText, wanted.userName, wanted.groupName, wanted.haveUser, wanted.haveGroup);
                printf("failed to change ownership of %s to %s\n", quote(operands[i]), newText);
            }
            continue;
        }
        process(operands[i], native, 1);
    }
    return ok ? 0 : 1;
}
