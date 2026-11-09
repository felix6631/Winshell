/*
 * install: copies files and sets their mode, or makes directories, as GNU install does: SOURCE to DEST, SOURCE... to a
 * DIRECTORY, -t, -T, -d (directories), -D (leading directories), -m MODE (default 0755), backups (-b, --backup, -S),
 * -C (leave an identical file alone), -p (times), -v. Windows keeps only the write permission, as chmod does: a mode
 * without any write bit makes the file read-only. The owner and the group are checked as names but not set, -s (strip)
 * is accepted and does nothing, and SELinux options are accepted and ignored.
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
#include "modebits.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "install"

static void print_usage(void) {
    puts("Usage: install [OPTION]... [-T] SOURCE DEST\n"
         "  or:  install [OPTION]... SOURCE... DIRECTORY\n"
         "  or:  install [OPTION]... -t DIRECTORY SOURCE...\n"
         "  or:  install [OPTION]... -d DIRECTORY...\n"
         "\n"
         "This install program copies files (often just compiled) into destination\n"
         "locations you choose.  If you want to download and install a ready-to-use\n"
         "package on a GNU/Linux system, you should instead be using a package manager\n"
         "like yum(1) or apt-get(1).\n"
         "\n"
         "In the first three forms, copy SOURCE to DEST or multiple SOURCE(s) to\n"
         "the existing DIRECTORY, while setting permission modes and owner/group.\n"
         "In the 4th form, create all components of the given DIRECTORY(ies).\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "      --backup[=CONTROL]  make a backup of each existing destination file\n"
         "  -b                  like --backup but does not accept an argument\n"
         "  -c                  (ignored)\n"
         "  -C, --compare       compare each pair of source and destination files, and\n"
         "                        in some cases, do not modify the destination at all\n"
         "  -d, --directory     treat all arguments as directory names; create all\n"
         "                        components of the specified directories\n"
         "  -D                  create all leading components of DEST except the last,\n"
         "                        or all components of --target-directory,\n"
         "                        then copy SOURCE to DEST\n"
         "  -g, --group=GROUP   set group ownership, instead of process' current group\n"
         "  -m, --mode=MODE     set permission mode (as in chmod), instead of rwxr-xr-x\n"
         "  -o, --owner=OWNER   set ownership (super-user only)\n"
         "  -p, --preserve-timestamps   apply access/modification times of SOURCE files\n"
         "                        to corresponding destination files\n"
         "  -s, --strip         strip symbol tables\n"
         "      --strip-program=PROGRAM  program used to strip binaries\n"
         "  -S, --suffix=SUFFIX  override the usual backup suffix\n"
         "  -t, --target-directory=DIRECTORY  copy all SOURCE arguments into DIRECTORY\n"
         "  -T, --no-target-directory  treat DEST as a normal file\n"
         "  -v, --verbose       print the name of each directory as it is created\n"
         "      --preserve-context  preserve SELinux security context\n"
         "  -Z                      set SELinux security context of destination\n"
         "                            file and each created directory to default type\n"
         "      --context[=CTX]     like -Z, or if CTX is specified then set the\n"
         "                            SELinux or SMACK security context to CTX\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "The backup suffix is '~', unless set with --suffix or SIMPLE_BACKUP_SUFFIX.\n"
         "The version control method may be selected via the --backup option or through\n"
         "the VERSION_CONTROL environment variable.  Here are the values:\n"
         "\n"
         "  none, off       never make backups (even if --backup is given)\n"
         "  numbered, t     make numbered backups\n"
         "  existing, nil   numbered if numbered backups exist, simple otherwise\n"
         "  simple, never   always make simple backups");
}

enum {
    OPT_BACKUP = 1, OPT_B, OPT_C_IGNORED, OPT_COMPARE, OPT_DIRECTORY, OPT_D, OPT_GROUP, OPT_MODE, OPT_OWNER, OPT_PRESERVE,
    OPT_STRIP, OPT_STRIP_PROGRAM, OPT_SUFFIX, OPT_TARGET, OPT_NO_TARGET, OPT_VERBOSE, OPT_PRESERVE_CONTEXT, OPT_Z, OPT_CONTEXT
};

static const ArgsOption options[] = {
    { OPT_BACKUP,           0,   "backup",            ARGS_OPTIONAL },
    { OPT_B,                'b', NULL,                ARGS_NONE },
    { OPT_C_IGNORED,        'c', NULL,                ARGS_NONE },
    { OPT_COMPARE,          'C', "compare",           ARGS_NONE },
    { OPT_DIRECTORY,        'd', "directory",         ARGS_NONE },
    { OPT_D,                'D', NULL,                ARGS_NONE },
    { OPT_GROUP,            'g', "group",             ARGS_REQUIRED },
    { OPT_MODE,             'm', "mode",              ARGS_REQUIRED },
    { OPT_OWNER,            'o', "owner",             ARGS_REQUIRED },
    { OPT_PRESERVE,         'p', "preserve-timestamps", ARGS_NONE },
    { OPT_STRIP,            's', "strip",             ARGS_NONE },
    { OPT_STRIP_PROGRAM,    0,   "strip-program",     ARGS_REQUIRED },
    { OPT_SUFFIX,           'S', "suffix",            ARGS_REQUIRED },
    { OPT_TARGET,           't', "target-directory",  ARGS_REQUIRED },
    { OPT_NO_TARGET,        'T', "no-target-directory", ARGS_NONE },
    { OPT_VERBOSE,          'v', "verbose",           ARGS_NONE },
    { OPT_PRESERVE_CONTEXT, 0,   "preserve-context",  ARGS_NONE },
    { OPT_Z,                'Z', NULL,                ARGS_NONE },
    { OPT_CONTEXT,          0,   "context",           ARGS_OPTIONAL },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef enum { BACKUP_NONE, BACKUP_NUMBERED, BACKUP_EXISTING, BACKUP_SIMPLE } BackupType;

static int backupWanted, compare, directoryMode, leading, preserveTimes, verbose, noTargetDirectory;
static BackupType backupType = BACKUP_NONE;
static const char* backupArg;
static const char* suffix;
static const char* targetDirectory;
static const char* modeText;
static const char* ownerName;
static const char* groupName;

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

static int on_option(int id, const char* value, void* context) {
    (void)context;
    switch (id) {
    case OPT_BACKUP: backupWanted = 1; backupArg = value; break;
    case OPT_B: backupWanted = 1; break;
    case OPT_COMPARE: compare = 1; break;
    case OPT_DIRECTORY: directoryMode = 1; break;
    case OPT_D: leading = 1; break;
    case OPT_GROUP: groupName = value; break;
    case OPT_MODE: modeText = value; break;
    case OPT_OWNER: ownerName = value; break;
    case OPT_PRESERVE: preserveTimes = 1; break;
    case OPT_SUFFIX: suffix = value; backupWanted = 1; break;
    case OPT_TARGET: targetDirectory = value; break;
    case OPT_NO_TARGET: noTargetDirectory = 1; break;
    case OPT_VERBOSE: verbose = 1; break;
    case OPT_PRESERVE_CONTEXT:
        fprintf(stderr, PROGRAM ": WARNING: ignoring --preserve-context; this kernel is not SELinux-enabled\n");
        break;
    default: break;
    }
    return 0;
}

/* ---- the backup control ---- */

static void choose_backup_type(void) {
    static const char* const names[] = { "none", "off", "simple", "never", "existing", "nil", "numbered", "t" };
    static const BackupType types[] = { BACKUP_NONE, BACKUP_NONE, BACKUP_SIMPLE, BACKUP_SIMPLE, BACKUP_EXISTING, BACKUP_EXISTING,
                                        BACKUP_NUMBERED, BACKUP_NUMBERED };
    const char* control = backupArg;
    size_t length;
    int i, found = -1, ambiguous = 0;

    if (!backupWanted)
        return;
    if (control == NULL || *control == '\0')
        control = getenv("VERSION_CONTROL");
    if (control == NULL || *control == '\0') {
        backupType = BACKUP_EXISTING;
        return;
    }
    length = strlen(control);
    for (i = 0; i < 8; i++) {
        if (strncmp(names[i], control, length) == 0) {
            if (strlen(names[i]) == length) {
                found = i;
                ambiguous = 0;
                break;
            }
            if (found < 0 || types[found] != types[i])
                ambiguous = found >= 0;
            if (found < 0)
                found = i;
        }
    }
    if (found < 0 || ambiguous) {
        fprintf(stderr, PROGRAM ": %s argument %s for 'backup type'\nValid arguments are:\n"
                        "  - 'none', 'off'\n  - 'simple', 'never'\n  - 'existing', 'nil'\n  - 'numbered', 't'\n",
                ambiguous ? "ambiguous" : "invalid", quote(control));
        args_usage_error(&spec);
        exit(1);
    }
    backupType = types[found];
}

/** The highest N of the numbered backups "name.~N~" in the directory of the file; 0 if there are none. */
static unsigned long highest_numbered(const char* nativeName) {
    char directory[VPATH_MAX], base[VPATH_MAX], pattern[VPATH_MAX];
    const char* slash = strrchr(nativeName, '\\');
    WIN32_FIND_DATAA data;
    HANDLE find;
    unsigned long highest = 0;
    size_t baseLength;

    if (slash == NULL)
        slash = strrchr(nativeName, '/');
    if (slash != NULL) {
        snprintf(directory, sizeof directory, "%.*s", (int)(slash - nativeName + 1), nativeName);
        snprintf(base, sizeof base, "%s", slash + 1);
    } else {
        directory[0] = '\0';
        snprintf(base, sizeof base, "%s", nativeName);
    }
    baseLength = strlen(base);
    snprintf(pattern, sizeof pattern, "%s%s.~*~", directory, base);
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return 0;
    do {
        const char* p = data.cFileName + baseLength;
        char* end;
        unsigned long n;

        if (strncmp(p, ".~", 2) != 0)
            continue;
        n = strtoul(p + 2, &end, 10);
        if (end != p + 2 && strcmp(end, "~") == 0 && n > highest)
            highest = n;
    } while (FindNextFileA(find, &data));
    FindClose(find);
    return highest;
}

/** The name of the backup of a file (display and native), by the backup type. */
static void backup_names(const char* display, const char* native, char* backupDisplay, char* backupNative, size_t size) {
    BackupType type = backupType;
    const char* suf = suffix;

    if (suf == NULL)
        suf = getenv("SIMPLE_BACKUP_SUFFIX");
    if (suf == NULL || *suf == '\0')
        suf = "~";
    if (type == BACKUP_EXISTING)
        type = highest_numbered(native) > 0 ? BACKUP_NUMBERED : BACKUP_SIMPLE;
    if (type == BACKUP_NUMBERED) {
        unsigned long n = highest_numbered(native) + 1;

        snprintf(backupDisplay, size, "%s.~%lu~", display, n);
        snprintf(backupNative, size, "%s.~%lu~", native, n);
    } else {
        snprintf(backupDisplay, size, "%s%s", display, suf);
        snprintf(backupNative, size, "%s%s", native, suf);
    }
}

/* ---- directories ---- */

static unsigned wantedMode = 0755;

/** Creates the directory and the missing ones above it; returns 0, or 1 after a message. */
static int make_directories(const char* display, const char* native) {
    char prefix[VPATH_MAX], nativePrefix[VPATH_MAX];
    size_t i, length = strlen(display);
    DWORD attributes;

    (void)native;
    while (length > 1 && (display[length - 1] == '/' || display[length - 1] == '\\'))
        length--;
    for (i = 1; i <= length; i++) {
        if (i == length || display[i] == '/' || display[i] == '\\') {
            snprintf(prefix, sizeof prefix, "%.*s", (int)i, display);
            if (vpath_to_native(prefix, nativePrefix, sizeof nativePrefix) != 0)
                continue;
            if (fs_attributes(nativePrefix, &attributes) != 0) {
                if (!CreateDirectoryA(nativePrefix, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                    fprintf(stderr, PROGRAM ": cannot create directory %s: %s\n", quote(prefix), fs_strerror(GetLastError()));
                    return 1;
                }
                if (verbose)
                    printf(PROGRAM ": creating directory %s\n", quote(prefix));
            } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                fprintf(stderr, PROGRAM ": cannot create directory %s: %s\n", quote(prefix), i == length ? "File exists" : "Not a directory");
                return 1;
            }
        }
    }
    return 0;
}

/* ---- files ---- */

static int same_content(const char* nativeA, const char* nativeB) {
    FILE* a = fopen(nativeA, "rb");
    FILE* b = fopen(nativeB, "rb");
    int same = 1;
    unsigned char bufA[65536], bufB[65536];

    if (a == NULL || b == NULL) {
        if (a != NULL)
            fclose(a);
        if (b != NULL)
            fclose(b);
        return 0;
    }
    for (;;) {
        size_t na = fread(bufA, 1, sizeof bufA, a), nb = fread(bufB, 1, sizeof bufB, b);

        if (na != nb || memcmp(bufA, bufB, na) != 0) {
            same = 0;
            break;
        }
        if (na == 0)
            break;
    }
    fclose(a);
    fclose(b);
    return same;
}

static int install_file(const char* source, const char* sourceNative, const char* dest, const char* destNative) {
    DWORD sourceAttributes, destAttributes = 0;
    int destExists;
    char backupDisplay[VPATH_MAX], backupNative[VPATH_MAX];
    int backedUp = 0;
    FsOptions copyOptions;

    if (fs_attributes(sourceNative, &sourceAttributes) != 0) {
        fprintf(stderr, PROGRAM ": cannot stat %s: No such file or directory\n", quote(source));
        return 1;
    }
    if (sourceAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        fprintf(stderr, PROGRAM ": omitting directory %s\n", quote(source));
        return 1;
    }
    destExists = fs_attributes(destNative, &destAttributes) == 0;
    if (destExists && (destAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        fprintf(stderr, PROGRAM ": cannot overwrite directory %s with non-directory\n", quote(dest));
        return 1;
    }
    if (destExists && fs_same_file(sourceNative, destNative)) {
        if (compare)
            return 0;
        fprintf(stderr, PROGRAM ": %s and %s are the same file\n", quote(source), quote(dest));
        return 1;
    }
    if (destExists && compare) {
        // identical content and the mode asked for: nothing to do
        unsigned destMode = mode_of_attributes(destAttributes, dest) & 07777;
        unsigned effective = (wantedMode & 0222) == 0 ? ((wantedMode & 0111) ? 0555 : 0444) : ((wantedMode & 0111) ? 0755 : 0644);

        if (destMode == (effective & 07777) && same_content(sourceNative, destNative))
            return 0;
    }
    if (destExists) {
        if (backupType != BACKUP_NONE) {
            backup_names(dest, destNative, backupDisplay, backupNative, sizeof backupDisplay);
            if (destAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesA(destNative, destAttributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
            if (!MoveFileExA(destNative, backupNative, MOVEFILE_REPLACE_EXISTING)) {
                fprintf(stderr, PROGRAM ": cannot backup %s: %s\n", quote(dest), fs_strerror(GetLastError()));
                return 1;
            }
            backedUp = 1;
        } else {
            if (destAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesA(destNative, destAttributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
            if (!DeleteFileA(destNative)) {
                fprintf(stderr, PROGRAM ": cannot remove %s: %s\n", quote(dest), fs_strerror(GetLastError()));
                return 1;
            }
            if (verbose)
                printf("removed %s\n", quote(dest));
        }
    }
    memset(&copyOptions, 0, sizeof copyOptions);
    copyOptions.program = PROGRAM;
    copyOptions.preserve = preserveTimes;
    if (verbose) {
        if (backedUp)
            printf("%s -> %s (backup: %s)\n", quote(source), quote(dest), quote(backupDisplay));
        else
            printf("%s -> %s\n", quote(source), quote(dest));
        fflush(stdout);
    }
    if (!CopyFileA(sourceNative, destNative, FALSE)) {
        DWORD error = GetLastError();

        fprintf(stderr, PROGRAM ": cannot create regular file %s: %s\n", quote(dest), fs_strerror(error));
        return 1;
    }
    {
        DWORD attributes = GetFileAttributesA(destNative);

        if (attributes != INVALID_FILE_ATTRIBUTES) {
            int readOnly = (wantedMode & 0222) == 0;
            DWORD wanted = readOnly ? attributes | FILE_ATTRIBUTE_READONLY : attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY;

            if (wanted == 0)
                wanted = FILE_ATTRIBUTE_NORMAL;
            if (wanted != attributes)
                SetFileAttributesA(destNative, wanted);
        }
    }
    if (preserveTimes) {
        HANDLE in = CreateFileA(sourceNative, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

        if (in != INVALID_HANDLE_VALUE) {
            FILETIME created, accessed, written;

            if (GetFileTime(in, &created, &accessed, &written)) {
                DWORD attributes = GetFileAttributesA(destNative);
                HANDLE outFile;

                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY))
                    SetFileAttributesA(destNative, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
                outFile = CreateFileA(destNative, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
                if (outFile != INVALID_HANDLE_VALUE) {
                    SetFileTime(outFile, NULL, &accessed, &written);
                    CloseHandle(outFile);
                }
                if (attributes != INVALID_FILE_ATTRIBUTES)
                    SetFileAttributesA(destNative, attributes);
            }
            CloseHandle(in);
        }
    }
    return 0;
}

static int is_directory(const char* native) {
    DWORD attributes;

    return fs_attributes(native, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, i, status = 0;
    ModeChange* changes;

    _setmode(_fileno(stdout), _O_BINARY);
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    choose_backup_type();
    if (modeText != NULL) {
        changes = mode_compile(modeText);
        if (changes == NULL) {
            fprintf(stderr, PROGRAM ": invalid mode %s\n", quote(modeText));
            return 1;
        }
        wantedMode = mode_adjust(0, directoryMode, 0, changes);
        mode_free(changes);
    }
    if (ownerName != NULL) {
        Identity id;

        if (ident_user(ownerName, &id) != 0) {
            fprintf(stderr, PROGRAM ": invalid user %s\n", quote(ownerName));
            return 1;
        }
    }
    if (groupName != NULL) {
        BYTE sid[SECURITY_MAX_SID_SIZE];
        DWORD sidSize = sizeof sid, domainSize = 256;
        char domain[256];
        SID_NAME_USE use;

        if (!LookupAccountNameA(NULL, groupName, (PSID)sid, &sidSize, domain, &domainSize, &use) ||
            (use != SidTypeGroup && use != SidTypeAlias && use != SidTypeWellKnownGroup)) {
            fprintf(stderr, PROGRAM ": invalid group %s\n", quote(groupName));
            return 1;
        }
    }
    if (directoryMode) {
        if (count == 0) {
            fprintf(stderr, PROGRAM ": missing file operand\n");
            args_usage_error(&spec);
            return 1;
        }
        for (i = 0; i < count; i++) {
            char native[VPATH_MAX];

            if (vpath_to_native(operands[i], native, sizeof native) != 0) {
                fprintf(stderr, PROGRAM ": cannot create directory %s: No such file or directory\n", quote(operands[i]));
                status = 1;
                continue;
            }
            if (is_directory(native))
                continue;
            if (make_directories(operands[i], native) != 0)
                status = 1;
        }
        return status;
    }
    if (count == 0 && targetDirectory == NULL) {
        fprintf(stderr, PROGRAM ": missing file operand\n");
        args_usage_error(&spec);
        return 1;
    }
    {
        const char* target;
        char targetNative[VPATH_MAX];
        int sourceCount, targetIsDirectory;

        if (targetDirectory != NULL) {
            target = targetDirectory;
            sourceCount = count;
        } else {
            if (count == 1) {
                fprintf(stderr, PROGRAM ": missing destination file operand after %s\n", quote(operands[0]));
                args_usage_error(&spec);
                return 1;
            }
            target = operands[count - 1];
            sourceCount = count - 1;
        }
        if (noTargetDirectory && count > 2) {
            fprintf(stderr, PROGRAM ": extra operand %s\n", quote(operands[2]));
            args_usage_error(&spec);
            return 1;
        }
        if (vpath_to_native(target, targetNative, sizeof targetNative) != 0) {
            fprintf(stderr, PROGRAM ": failed to access %s: No such file or directory\n", quote(target));
            return 1;
        }
        targetIsDirectory = is_directory(targetNative);
        if (targetDirectory != NULL) {
            if (!targetIsDirectory && leading && make_directories(target, targetNative) == 0)
                targetIsDirectory = 1;
            if (!targetIsDirectory) {
                DWORD attributes;

                if (fs_attributes(targetNative, &attributes) != 0)
                    fprintf(stderr, PROGRAM ": failed to access %s: No such file or directory\n", quote(target));
                else
                    fprintf(stderr, PROGRAM ": target %s is not a directory\n", quote(target));
                return 1;
            }
        } else if (sourceCount > 1 && !targetIsDirectory) {
            fprintf(stderr, PROGRAM ": target %s is not a directory\n", quote(target));
            return 1;
        }
        if (targetDirectory != NULL && count == 0) {
            fprintf(stderr, PROGRAM ": missing file operand\n");
            args_usage_error(&spec);
            return 1;
        }
        if (targetDirectory == NULL && !targetIsDirectory && sourceCount == 1) {
            size_t tl = strlen(target);

            if (tl > 0 && (target[tl - 1] == '/' || target[tl - 1] == '\\') && !noTargetDirectory) {
                fprintf(stderr, PROGRAM ": target %s is not a directory: No such file or directory\n", quote(target));
                return 1;
            }
        }
        if (noTargetDirectory)
            targetIsDirectory = 0;
        for (i = 0; i < sourceCount; i++) {
            char sourceNative[VPATH_MAX], destDisplay[VPATH_MAX], destNative[VPATH_MAX], base[VPATH_MAX];
            const char* source = operands[i];

            if (vpath_to_native(source, sourceNative, sizeof sourceNative) != 0) {
                fprintf(stderr, PROGRAM ": cannot stat %s: No such file or directory\n", quote(source));
                status = 1;
                continue;
            }
            if (targetIsDirectory) {
                fs_basename(source, base, sizeof base);
                fs_join(target, base, destDisplay, sizeof destDisplay);
                fs_join(targetNative, base, destNative, sizeof destNative);
            } else {
                snprintf(destDisplay, sizeof destDisplay, "%s", target);
                snprintf(destNative, sizeof destNative, "%s", targetNative);
                if (leading && !targetIsDirectory) {
                    char parentDisplay[VPATH_MAX], parentNative[VPATH_MAX];
                    char* slash;

                    snprintf(parentDisplay, sizeof parentDisplay, "%s", target);
                    snprintf(parentNative, sizeof parentNative, "%s", targetNative);
                    slash = strrchr(parentDisplay, '/');
                    if (slash != NULL && slash != parentDisplay) {
                        char* nativeSlash = strrchr(parentNative, '\\');

                        *slash = '\0';
                        if (nativeSlash == NULL)
                            nativeSlash = strrchr(parentNative, '/');
                        if (nativeSlash != NULL)
                            *nativeSlash = '\0';
                        if (!is_directory(parentNative) && make_directories(parentDisplay, parentNative) != 0) {
                            status = 1;
                            continue;
                        }
                    }
                }
            }
            if (install_file(source, sourceNative, destDisplay, destNative) != 0)
                status = 1;
        }
    }
    return status;
}
