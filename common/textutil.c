#include "textutil.h"
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"
#include "fsutil.h"

int text_parse_count(const char* text, TextCount* out) {
    const char* p = text;
    unsigned long long value = 0;
    unsigned long long scale = 1;
    int digits = 0;

    memset(out, 0, sizeof *out);
    if (*p == '+') {
        out->plus = 1;
        p++;
    } else if (*p == '-') {
        out->minus = 1;
        p++;
    }

    while (*p >= '0' && *p <= '9') {
        unsigned long long next = value * 10 + (unsigned long long)(*p - '0');
        if (next < value || next > 0x3FFFFFFFFFFFFFFFULL)
            next = 0x3FFFFFFFFFFFFFFFULL;       // far larger than any file
        value = next;
        digits++;
        p++;
    }
    if (digits == 0)
        return -1;

    if (*p != '\0') {
        int decimal = 0;
        char suffix = *p++;

        if (*p == 'B' && p[1] == '\0') {
            decimal = 1;
            p++;
        }
        if (*p != '\0')
            return -1;

        {
            unsigned long long base = decimal ? 1000ULL : 1024ULL;
            switch (suffix) {
                case 'b': scale = decimal ? 0 : 512ULL; break;
                case 'k':
                case 'K': scale = base; break;
                case 'm':
                case 'M': scale = base * base; break;
                case 'g':
                case 'G': scale = base * base * base; break;
                case 't':
                case 'T': scale = base * base * base * base; break;
                default:  scale = 0; break;
            }
        }
        if (scale == 0)
            return -1;
        if (value > 0x3FFFFFFFFFFFFFFFULL / scale)
            value = 0x3FFFFFFFFFFFFFFFULL;
        else
            value *= scale;
    }

    out->value = (long long)value;
    return 0;
}

int text_open(const char* program, const char* path, int plainMessage, FILE** out) {
    char native[VPATH_MAX];
    int kind;
    FILE* file;
    const char* reason;

    if (strcmp(path, "-") == 0) {
        *out = stdin;
        return 0;
    }

    kind = vpath_to_native(path, native, sizeof native);
    if (kind == VPATH_ROOT) {
        reason = "Is a directory";
    } else if (kind != 0) {
        reason = strerror(errno);
    } else {
        DWORD attributes;
        if (fs_attributes(native, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            reason = "Is a directory";
        } else {
            file = fopen(native, "rb");
            if (file != NULL) {
                *out = file;
                return 0;
            }
            reason = fs_strerror(GetLastError());
            if (errno == ENOENT)
                reason = "No such file or directory";
        }
    }

    if (plainMessage)
        fprintf(stderr, "%s: %s: %s\n", program, path, reason);
    else
        fprintf(stderr, "%s: cannot open '%s' for reading: %s\n", program, path, reason);
    return -1;
}

void text_binary_streams(void) {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
}
