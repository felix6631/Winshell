#include "textutil.h"
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "vpath.h"
#include "fsutil.h"
#include "vec.h"

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

long text_read_some(FILE* in, void* buffer, size_t size) {
    int got;
    if (size > 0x7FFF0000U)
        size = 0x7FFF0000U;
    got = _read(_fileno(in), buffer, (unsigned int)size);
    return got < 0 ? -1 : (long)got;
}

unsigned char* text_slurp(FILE* in, size_t* length) {
    size_t capacity = 0, used = 0;
    unsigned char* data = NULL;

    if (VEC_RESERVE(data, capacity, 65536) != 0) {
        *length = 0;
        return NULL;
    }

    for (;;) {
        size_t got;
        if (used == capacity && VEC_RESERVE(data, capacity, capacity + 1) != 0) {
            free(data);
            data = NULL;
            break;
        }
        got = fread(data + used, 1, capacity - used, in);
        if (got == 0)
            break;
        used += got;
    }
    *length = used;
    return data;
}

int text_next_line(const unsigned char* text, size_t length, size_t* offset, TextLine* line) {
    size_t start = *offset, i = start;

    if (start >= length)
        return 0;
    while (i < length && text[i] != '\n')
        i++;

    line->content = text + start;
    if (i < length) {
        // a line feed ends the line; a CR in front of it belongs to the ending
        if (i > start && text[i - 1] == '\r') {
            line->length = i - start - 1;
            line->ending = 2;
        } else {
            line->length = i - start;
            line->ending = 1;
        }
        *offset = i + 1;
    } else {
        line->length = i - start;
        line->ending = 0;
        *offset = length;
    }
    return 1;
}

int text_write_line(const TextLine* line, FILE* out) {
    if (line->length > 0 && fwrite(line->content, 1, line->length, out) != line->length)
        return -1;
    if (line->ending == 2)
        return fputs("\r\n", out) < 0 ? -1 : 0;
    return fputc('\n', out) == EOF ? -1 : 0;
}

void text_reader_init(LineReader* reader, FILE* in, int terminator) {
    memset(reader, 0, sizeof *reader);
    reader->in = in;
    reader->terminator = terminator;
}

void text_reader_free(LineReader* reader) {
    free(reader->buffer);
    memset(reader, 0, sizeof *reader);
}

int text_reader_next(LineReader* reader, TextLine* line) {
    for (;;) {
        unsigned char* begin = reader->buffer + reader->start;
        size_t available = reader->length - reader->start;
        unsigned char* end = NULL;

        if (reader->scanned < available)
            end = memchr(begin + reader->scanned, reader->terminator, available - reader->scanned);

        if (end != NULL) {
            size_t contentLength = (size_t)(end - begin);
            line->content = begin;
            line->length = contentLength;
            line->ending = 1;
            if (reader->terminator == '\n' && contentLength > 0 && begin[contentLength - 1] == '\r') {
                line->length--;
                line->ending = 2;
            }
            reader->start += contentLength + 1;
            reader->scanned = 0;
            return 1;
        }
        reader->scanned = available;

        if (reader->eof || reader->failed) {
            if (available == 0)
                return 0;
            line->content = begin;
            line->length = available;
            line->ending = 0;
            reader->start = reader->length;
            reader->scanned = 0;
            return 1;
        }

        // more input: keep the unfinished line at the front of the buffer
        if (reader->start > 0) {
            memmove(reader->buffer, begin, available);
            reader->length = available;
            reader->start = 0;
        }
        if (VEC_RESERVE(reader->buffer, reader->capacity, reader->length + 4096) != 0) {
            reader->failed = 1;
            continue;
        }
        {
            long got = text_read_some(reader->in, reader->buffer + reader->length,
                                      reader->capacity - reader->length);
            if (got < 0)
                reader->failed = 1;
            else if (got == 0)
                reader->eof = 1;
            else
                reader->length += (size_t)got;
        }
    }
}
