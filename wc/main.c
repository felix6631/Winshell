#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "textutil.h"
#include "vpath.h"
#include "fsutil.h"

#define VERSION "0.1"
#define PROGRAM "wc"

static void print_usage(void) {
    puts("Usage: wc [OPTION]... [FILE]...\n"
         "Print newline, word, and byte counts for each FILE, and a total line if\n"
         "more than one FILE is specified.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "  -c, --bytes            print the byte counts\n"
         "  -m, --chars            print the character counts (UTF-8)\n"
         "  -l, --lines            print the newline counts\n"
         "  -L, --max-line-length  print the maximum display width\n"
         "  -w, --words            print the word counts\n"
         "      --help             display this help and exit\n"
         "      --version          output version information and exit\n"
         "\n"
         "Lines are counted by their line feeds, so files with LF and with CR LF line\n"
         "endings give the same line count; a CR counts as white space between words\n"
         "and does not add to the width of a line.");
}

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

typedef struct {
    unsigned long long lines, words, chars, bytes, maxWidth;
} Counts;

static int show_lines, show_words, show_chars, show_bytes, show_width;

static int is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static int count_stream(FILE* in, Counts* counts) {
    unsigned char buffer[65536];
    int inWord = 0;
    unsigned long long width = 0;
    size_t got, i;

    memset(counts, 0, sizeof *counts);

    while ((got = fread(buffer, 1, sizeof buffer, in)) > 0) {
        counts->bytes += got;
        for (i = 0; i < got; i++) {
            unsigned char c = buffer[i];

            // characters: every byte that is not a continuation byte of UTF-8
            if ((c & 0xC0) != 0x80)
                counts->chars++;

            if (c == '\n')
                counts->lines++;

            if (is_space(c)) {
                inWord = 0;
            } else if (!inWord) {
                inWord = 1;
                counts->words++;
            }

            // the display width of a line; a line feed or CR ends it, so CR LF is no wider than LF
            if (c == '\n' || c == '\r' || c == '\f') {
                if (width > counts->maxWidth)
                    counts->maxWidth = width;
                width = 0;
            } else if (c == '\t') {
                width = (width / 8 + 1) * 8;
            } else if ((c & 0xC0) != 0x80 && c >= 0x20 && c != 0x7F) {
                width++;
            }
        }
    }
    if (width > counts->maxWidth)
        counts->maxWidth = width;
    return ferror(in) ? -1 : 0;
}

static int digits_of(unsigned long long value) {
    int digits = 1;
    while (value >= 10) {
        value /= 10;
        digits++;
    }
    return digits;
}

static void print_counts(const Counts* counts, int width, const char* name) {
    int first = 1;

    #define NUMBER(value) \
        do { \
            printf("%s%*llu", first ? "" : " ", width, (unsigned long long)(value)); \
            first = 0; \
        } while (0)

    if (show_lines) NUMBER(counts->lines);
    if (show_words) NUMBER(counts->words);
    if (show_chars) NUMBER(counts->chars);
    if (show_bytes) NUMBER(counts->bytes);
    if (show_width) NUMBER(counts->maxWidth);
    #undef NUMBER

    if (name != NULL)
        printf(" %s", name);
    putchar('\n');
}

int main(int argc, char* argv[]) {
    const char** operands = malloc((size_t)(argc + 1) * sizeof *operands);
    int operandCount = 0, optionsEnded = 0, status = 0, i, columns, width;
    unsigned long long totalSize = 0;
    int stdinInvolved = 0;
    Counts total;

    if (operands == NULL)
        return 1;
    memset(&total, 0, sizeof total);

    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (optionsEnded || arg[0] != '-' || arg[1] == '\0') {
            operands[operandCount++] = arg;
        } else if (strcmp(arg, "--") == 0) {
            optionsEnded = 1;
        } else if (arg[1] == '-') {
            if (strcmp(arg, "--help") == 0) {
                print_usage();
                return 0;
            } else if (strcmp(arg, "--version") == 0) {
                puts(PROGRAM " (Winshell) " VERSION);
                return 0;
            } else if (strcmp(arg, "--bytes") == 0) {
                show_bytes = 1;
            } else if (strcmp(arg, "--chars") == 0) {
                show_chars = 1;
            } else if (strcmp(arg, "--lines") == 0) {
                show_lines = 1;
            } else if (strcmp(arg, "--words") == 0) {
                show_words = 1;
            } else if (strcmp(arg, "--max-line-length") == 0) {
                show_width = 1;
            } else {
                fprintf(stderr, PROGRAM ": unrecognized option '%s'\n", arg);
                try_help();
                return 1;
            }
        } else {
            const char* p;
            for (p = arg + 1; *p != '\0'; p++) {
                switch (*p) {
                    case 'c': show_bytes = 1; break;
                    case 'm': show_chars = 1; break;
                    case 'l': show_lines = 1; break;
                    case 'w': show_words = 1; break;
                    case 'L': show_width = 1; break;
                    default:
                        fprintf(stderr, PROGRAM ": invalid option -- '%c'\n", *p);
                        try_help();
                        return 1;
                }
            }
        }
    }

    if (!show_lines && !show_words && !show_chars && !show_bytes && !show_width)
        show_lines = show_words = show_bytes = 1;
    columns = show_lines + show_words + show_chars + show_bytes + show_width;

    text_binary_streams();

    // How wide the numbers are: as many digits as the sizes of the files add up to, or 7 when
    // input of unknown size (standard input) is involved. One number of one file is not padded.
    if (operandCount == 0)
        stdinInvolved = 1;
    for (i = 0; i < operandCount; i++) {
        char native[VPATH_MAX];
        WIN32_FILE_ATTRIBUTE_DATA data;

        if (strcmp(operands[i], "-") == 0) {
            stdinInvolved = 1;
        } else if (vpath_to_native(operands[i], native, sizeof native) == 0 &&
                   GetFileAttributesExA(native, GetFileExInfoStandard, &data)) {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                stdinInvolved = 1;      // not a regular file: its size says nothing, as for a pipe
            else
                totalSize += ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        }
    }
    width = stdinInvolved ? 7 : digits_of(totalSize);
    if (columns == 1 && operandCount <= 1)
        width = 0;

    if (operandCount == 0) {
        Counts counts;
        if (count_stream(stdin, &counts) != 0) {
            fprintf(stderr, PROGRAM ": read error\n");
            return 1;
        }
        print_counts(&counts, width, NULL);
        free(operands);
        return 0;
    }

    for (i = 0; i < operandCount; i++) {
        Counts counts;
        FILE* in;
        char native[VPATH_MAX];
        DWORD attributes;

        // a directory: reported, and counted as nothing, as GNU wc does
        if (strcmp(operands[i], "-") != 0 && vpath_to_native(operands[i], native, sizeof native) == 0 &&
            fs_attributes(native, &attributes) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            fprintf(stderr, PROGRAM ": %s: Is a directory\n", operands[i]);
            memset(&counts, 0, sizeof counts);
            print_counts(&counts, width, operands[i]);
            status = 1;
            continue;
        }

        if (text_open(PROGRAM, operands[i], 1, &in) != 0) {
            status = 1;
            continue;
        }

        if (count_stream(in, &counts) != 0) {
            fprintf(stderr, PROGRAM ": %s: read error\n", operands[i]);
            status = 1;
        }
        if (in != stdin)
            fclose(in);
        else
            clearerr(stdin);

        print_counts(&counts, width, operands[i]);

        total.lines += counts.lines;
        total.words += counts.words;
        total.chars += counts.chars;
        total.bytes += counts.bytes;
        if (counts.maxWidth > total.maxWidth)
            total.maxWidth = counts.maxWidth;
    }

    if (operandCount > 1)
        print_counts(&total, width, "total");

    free(operands);
    return status;
}
