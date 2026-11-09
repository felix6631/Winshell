/*
 * modebits: compiles and applies mode strings, after gnulib's modechange.c.
 */

#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "modebits.h"

#define S_IRUSR_ 0400
#define S_IWUSR_ 0200
#define S_IXUSR_ 0100
#define S_IRGRP_ 040
#define S_IWGRP_ 020
#define S_IXGRP_ 010
#define S_IROTH_ 04
#define S_IWOTH_ 02
#define S_IXOTH_ 01
#define S_IRWXU_ 0700
#define S_IRWXG_ 070
#define S_IRWXO_ 07
#define ALL_BITS 07777

enum { FLAG_ORDINARY = 0, FLAG_X_IF_ANY_X = 1, FLAG_COPY = 2, FLAG_DONE = 3 };

struct ModeChange {
    char     op;
    char     flag;
    unsigned affected;
    unsigned value;
    unsigned mentioned;
};

ModeChange* mode_compile(const char* text) {
    const char* p = text;
    ModeChange* changes;
    size_t used = 0;

    if (*p >= '0' && *p < '8') {
        unsigned mode = 0;

        do {
            mode = 8 * mode + (unsigned)(*p++ - '0');
            if (mode > 07777)
                return NULL;
        } while (*p >= '0' && *p < '8');
        if (*p != '\0')
            return NULL;
        changes = malloc(2 * sizeof *changes);
        if (changes == NULL)
            return NULL;
        changes[0].op = '=';
        changes[0].flag = FLAG_ORDINARY;
        changes[0].affected = ALL_BITS;
        changes[0].value = mode;
        changes[0].mentioned = ALL_BITS;
        changes[1].flag = FLAG_DONE;
        return changes;
    }
    changes = malloc((strlen(text) + 1) * sizeof *changes);
    if (changes == NULL)
        return NULL;
    for (;; p++) {
        unsigned affected = 0;

        for (;; p++) {
            switch (*p) {
            case 'u': affected |= MODE_SETUID | S_IRWXU_; continue;
            case 'g': affected |= MODE_SETGID | S_IRWXG_; continue;
            case 'o': affected |= MODE_STICKY | S_IRWXO_; continue;
            case 'a': affected |= ALL_BITS; continue;
            default: break;
            }
            break;
        }
        do {
            char op = *p;
            unsigned value = 0;
            char flag = FLAG_COPY;

            if (op != '=' && op != '+' && op != '-') {
                free(changes);
                return NULL;
            }
            p++;
            switch (*p) {
            case 'u': value = S_IRWXU_; p++; break;
            case 'g': value = S_IRWXG_; p++; break;
            case 'o': value = S_IRWXO_; p++; break;
            default:
                flag = FLAG_ORDINARY;
                for (;; p++) {
                    switch (*p) {
                    case 'r': value |= S_IRUSR_ | S_IRGRP_ | S_IROTH_; continue;
                    case 'w': value |= S_IWUSR_ | S_IWGRP_ | S_IWOTH_; continue;
                    case 'x': value |= S_IXUSR_ | S_IXGRP_ | S_IXOTH_; continue;
                    case 'X': flag = FLAG_X_IF_ANY_X; continue;
                    case 's': value |= MODE_SETUID | MODE_SETGID; continue;
                    case 't': value |= MODE_STICKY; continue;
                    default: break;
                    }
                    break;
                }
            }
            changes[used].op = op;
            changes[used].flag = flag;
            changes[used].affected = affected;
            changes[used].value = value;
            changes[used].mentioned = affected != 0 ? affected : flag == FLAG_COPY ? 0 : value;
            used++;
        } while (*p == '=' || *p == '-' || *p == '+');
        if (*p != ',')
            break;
    }
    if (*p != '\0') {
        free(changes);
        return NULL;
    }
    changes[used].flag = FLAG_DONE;
    return changes;
}

void mode_free(ModeChange* changes) {
    free(changes);
}

unsigned mode_adjust(unsigned oldMode, int directory, unsigned umaskValue, const ModeChange* changes) {
    unsigned newMode = oldMode & ALL_BITS;

    for (; changes->flag != FLAG_DONE; changes++) {
        unsigned affected = changes->affected;
        unsigned omit = (directory ? MODE_SETUID : MODE_SETGID) & ~changes->mentioned;
        unsigned value = changes->value;

        switch (changes->flag) {
        case FLAG_COPY:
            value &= newMode;
            value |= ((value & (S_IRUSR_ | S_IRGRP_ | S_IROTH_)) ? S_IRUSR_ | S_IRGRP_ | S_IROTH_ : 0)
                   | ((value & (S_IWUSR_ | S_IWGRP_ | S_IWOTH_)) ? S_IWUSR_ | S_IWGRP_ | S_IWOTH_ : 0)
                   | ((value & (S_IXUSR_ | S_IXGRP_ | S_IXOTH_)) ? S_IXUSR_ | S_IXGRP_ | S_IXOTH_ : 0);
            break;
        case FLAG_X_IF_ANY_X:
            if ((newMode & (S_IXUSR_ | S_IXGRP_ | S_IXOTH_)) != 0 || directory)
                value |= S_IXUSR_ | S_IXGRP_ | S_IXOTH_;
            break;
        default:
            break;
        }
        value &= (affected != 0 ? affected : ~umaskValue) & ~omit;
        switch (changes->op) {
        case '=': {
            unsigned preserved = (affected != 0 ? ~affected : 0) | omit;

            newMode = (newMode & preserved) | value;
            break;
        }
        case '+':
            newMode |= value;
            break;
        case '-':
            newMode &= ~value;
            break;
        }
    }
    return newMode & ALL_BITS;
}

void mode_text9(unsigned mode, char* out) {
    out[0] = (mode & S_IRUSR_) ? 'r' : '-';
    out[1] = (mode & S_IWUSR_) ? 'w' : '-';
    out[2] = (mode & MODE_SETUID) ? ((mode & S_IXUSR_) ? 's' : 'S') : ((mode & S_IXUSR_) ? 'x' : '-');
    out[3] = (mode & S_IRGRP_) ? 'r' : '-';
    out[4] = (mode & S_IWGRP_) ? 'w' : '-';
    out[5] = (mode & MODE_SETGID) ? ((mode & S_IXGRP_) ? 's' : 'S') : ((mode & S_IXGRP_) ? 'x' : '-');
    out[6] = (mode & S_IROTH_) ? 'r' : '-';
    out[7] = (mode & S_IWOTH_) ? 'w' : '-';
    out[8] = (mode & MODE_STICKY) ? ((mode & S_IXOTH_) ? 't' : 'T') : ((mode & S_IXOTH_) ? 'x' : '-');
    out[9] = '\0';
}

unsigned mode_of_attributes(unsigned long attributes, const char* name) {
    const char* dot;
    unsigned mode;

    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        return 0755;
    mode = (attributes & FILE_ATTRIBUTE_READONLY) ? 0444 : 0644;
    dot = strrchr(name, '.');
    if (dot != NULL && (_stricmp(dot, ".exe") == 0 || _stricmp(dot, ".bat") == 0 || _stricmp(dot, ".cmd") == 0 ||
                        _stricmp(dot, ".com") == 0 || _stricmp(dot, ".ps1") == 0))
        mode |= 0111;
    return mode;
}
