#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "checksum.h"
#include "textutil.h"
#include "vpath.h"

enum { OPT_BINARY = 1, OPT_CHECK, OPT_TEXT, OPT_WARN, OPT_ZERO, OPT_TAG, OPT_QUIET, OPT_STATUS, OPT_STRICT, OPT_IGNORE_MISSING };

static const ArgsOption options[] = {
    { OPT_BINARY,         'b', "binary",         ARGS_NONE },
    { OPT_CHECK,          'c', "check",          ARGS_NONE },
    { OPT_TEXT,           't', "text",           ARGS_NONE },
    { OPT_WARN,           'w', "warn",           ARGS_NONE },
    { OPT_ZERO,           'z', "zero",           ARGS_NONE },
    { OPT_TAG,            0,   "tag",            ARGS_NONE },
    { OPT_QUIET,          0,   "quiet",          ARGS_NONE },
    { OPT_STATUS,         0,   "status",         ARGS_NONE },
    { OPT_STRICT,         0,   "strict",         ARGS_NONE },
    { OPT_IGNORE_MISSING, 0,   "ignore-missing", ARGS_NONE },
    { 0 }
};

typedef struct {
    int binary, check, text, warn, zero, tag, quiet, status, strict, ignoreMissing;
} Settings;

static const char* currentProgram;
static const char* currentBits;

static void print_usage(void) {
    printf("Usage: %s [OPTION]... [FILE]...\n"
           "Print or check %s checksums.\n"
           "\n"
           "With no FILE, or when FILE is -, read standard input.\n"
           "\n"
           "  -b, --binary         read in binary mode (default)\n"
           "  -c, --check          read %s sums from the FILEs and check them\n"
           "      --tag            create a BSD-style checksum\n"
           "  -t, --text           read in text mode\n"
           "  -z, --zero           end each output line with NUL, not newline,\n"
           "                       and disable file name escaping\n"
           "\n"
           "The following five options are useful only when verifying checksums:\n"
           "      --ignore-missing  don't fail or report status for missing files\n"
           "      --quiet          don't print OK for each successfully verified file\n"
           "      --status         don't output anything, status code shows success\n"
           "      --strict         exit non-zero for improperly formatted checksum lines\n"
           "  -w, --warn           warn about improperly formatted checksum lines\n"
           "\n"
           "      --help     display this help and exit\n"
           "      --version  output version information and exit\n"
           "\n"
           "The default mode is to print a line with checksum, a space, a character\n"
           "indicating input mode ('*' for binary, ' ' for text), and name for each FILE.\n",
           currentProgram, currentBits, currentBits);
}

static int on_option(int id, const char* value, void* context) {
    Settings* s = context;

    (void)value;
    switch (id) {
        case OPT_BINARY: s->binary = 1; s->text = 0; break;
        case OPT_TEXT:   s->text = 1; s->binary = 0; break;
        case OPT_CHECK:  s->check = 1; break;
        case OPT_WARN:   s->warn = 1; break;
        case OPT_ZERO:   s->zero = 1; break;
        case OPT_TAG:    s->tag = 1; break;
        case OPT_QUIET:  s->quiet = 1; break;
        case OPT_STATUS: s->status = 1; break;
        case OPT_STRICT: s->strict = 1; break;
        case OPT_IGNORE_MISSING: s->ignoreMissing = 1; break;
    }
    return 0;
}

static const char hexDigits[] = "0123456789abcdef";

/** The digest of a stream as hexadecimal text; returns 0, or -1 on a read error. */
static int digest_stream(HashAlgorithm algorithm, FILE* in, char* hex) {
    HashContext context;
    unsigned char buffer[65536], out[64];
    long got;
    size_t i, size = hash_size(algorithm);

    hash_init(&context, algorithm);
    while ((got = text_read_some(in, buffer, sizeof buffer)) > 0)
        hash_update(&context, buffer, (size_t)got);
    if (got < 0)
        return -1;
    hash_final(&context, out);
    for (i = 0; i < size; i++) {
        hex[2 * i] = hexDigits[out[i] >> 4];
        hex[2 * i + 1] = hexDigits[out[i] & 15];
    }
    hex[2 * size] = '\0';
    return 0;
}

static void put_name(const char* name, int escape) {
    if (!escape) {
        fputs(name, stdout);
        return;
    }
    for (; *name != '\0'; name++) {
        if (*name == '\\')
            fputs("\\\\", stdout);
        else if (*name == '\n')
            fputs("\\n", stdout);
        else
            putchar(*name);
    }
}

static int make_sums(const Settings* s, const char* tag, HashAlgorithm algorithm, const char** operands, int count) {
    int i, status = 0;
    char hex[130];

    for (i = 0; i < count; i++) {
        FILE* in;
        int needsEscape;

        if (text_open(currentProgram, operands[i], 1, &in) != 0) {
            status = 1;
            continue;
        }
        if (digest_stream(algorithm, in, hex) != 0) {
            fprintf(stderr, "%s: %s: read error\n", currentProgram, operands[i]);
            status = 1;
            if (in != stdin)
                fclose(in);
            continue;
        }
        if (in != stdin)
            fclose(in);
        needsEscape = !s->zero && (strchr(operands[i], '\\') != NULL || strchr(operands[i], '\n') != NULL);
        if (needsEscape)
            putchar('\\');
        if (s->tag) {
            printf("%s (", tag);
            put_name(operands[i], needsEscape);
            printf(") = %s", hex);
        } else {
            printf("%s %c", hex, s->text ? ' ' : '*');
            put_name(operands[i], needsEscape);
        }
        putchar(s->zero ? '\0' : '\n');
    }
    return status;
}

/** One line of a checksum file: the hash, the name and the mode. Returns 1 when it is well formed. */
static int parse_line(char* line, const char* tag, size_t hexLength, char** hash, char** name, int* escaped) {
    char* s = line;
    size_t tagLength = strlen(tag), length;

    *escaped = 0;
    if (*s == '\\') {
        *escaped = 1;
        s++;
    }
    if (strncmp(s, tag, tagLength) == 0 && s[tagLength] == ' ' && s[tagLength + 1] == '(') {
        // BSD style: TAG (name) = hash
        char* nameStart = s + tagLength + 2;
        size_t total = strlen(nameStart);

        if (total < hexLength + 4)
            return 0;
        if (strncmp(nameStart + total - hexLength - 4, ") = ", 4) != 0)
            return 0;
        nameStart[total - hexLength - 4] = '\0';
        *hash = nameStart + total - hexLength;
        *name = nameStart;
        for (length = 0; length < hexLength; length++)
            if (!isxdigit((unsigned char)(*hash)[length]))
                return 0;
        return 1;
    }
    for (length = 0; length < hexLength; length++)
        if (!isxdigit((unsigned char)s[length]))
            return 0;
    if (s[hexLength] != ' ' || (s[hexLength + 1] != ' ' && s[hexLength + 1] != '*') || s[hexLength + 2] == '\0')
        return 0;
    *hash = s;
    s[hexLength] = '\0';
    *name = s + hexLength + 2;
    return 1;
}

static void unescape(char* name) {
    char* out = name;

    for (; *name != '\0'; name++) {
        if (*name == '\\' && name[1] == '\\') {
            *out++ = '\\';
            name++;
        } else if (*name == '\\' && name[1] == 'n') {
            *out++ = '\n';
            name++;
        } else {
            *out++ = *name;
        }
    }
    *out = '\0';
}

static int hex_equal(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return *a == *b;
}

static int check_sums(const Settings* s, const char* tag, HashAlgorithm algorithm, const char** operands, int count) {
    int i, status = 0;
    size_t hexLength = hash_size(algorithm) * 2;

    for (i = 0; i < count; i++) {
        FILE* in;
        unsigned char* data;
        size_t length, position = 0;
        long lineNumber = 0, bad = 0, unreadable = 0, mismatched = 0, verified = 0, missing = 0, wellFormed = 0;

        if (text_open(currentProgram, operands[i], 1, &in) != 0) {
            status = 1;
            continue;
        }
        data = text_slurp(in, &length);
        if (in != stdin)
            fclose(in);
        if (data == NULL) {
            status = 1;
            continue;
        }
        while (position < length) {
            size_t end = position;
            char* line;
            char* hash;
            char* name;
            int escaped;

            while (end < length && data[end] != '\n')
                end++;
            line = malloc(end - position + 1);
            if (line == NULL)
                break;
            memcpy(line, data + position, end - position);
            line[end - position] = '\0';
            position = end < length ? end + 1 : end;
            lineNumber++;
            if (line[0] == '#' || line[0] == '\0') {
                free(line);
                continue;
            }
            if (!parse_line(line, tag, hexLength, &hash, &name, &escaped)) {
                bad++;
                if (s->warn && !s->status)
                    fprintf(stderr, "%s: %s: %ld: improperly formatted %s checksum line\n", currentProgram, operands[i],
                            lineNumber, tag);
                free(line);
                continue;
            }
            wellFormed++;
            if (escaped)
                unescape(name);
            {
                FILE* target;
                char hex[130];
                int ok = 0, opened;

                if (strcmp(name, "-") == 0) {
                    target = stdin;
                    opened = 1;
                } else {
                    char native[VPATH_MAX];
                    int converted = vpath_to_native(name, native, sizeof native) == 0;

                    target = converted ? fopen(native, "rb") : NULL;
                    opened = target != NULL;
                    if (!opened) {
                        if (s->ignoreMissing && (!converted || GetFileAttributesA(native) == INVALID_FILE_ATTRIBUTES)) {
                            missing++;
                            free(line);
                            continue;
                        }
                        if (!s->status)
                            fprintf(stderr, "%s: %s: No such file or directory\n", currentProgram, name);
                    }
                }
                if (opened && target != NULL) {
                    ok = digest_stream(algorithm, target, hex) == 0 && hex_equal(hex, hash);
                    if (target != stdin)
                        fclose(target);
                }
                if (!opened) {
                    unreadable++;
                    if (!s->status) {
                        printf("%s: FAILED open or read\n", name);
                    }
                } else if (ok) {
                    verified++;
                    if (!s->status && !s->quiet)
                        printf("%s: OK\n", name);
                } else {
                    mismatched++;
                    if (!s->status)
                        printf("%s: FAILED\n", name);
                }
            }
            free(line);
        }
        free(data);
        fflush(stdout);

        if (wellFormed == 0 && bad >= 0 && !s->status) {
            fprintf(stderr, "%s: %s: no properly formatted checksum lines found\n", currentProgram, operands[i]);
            status = 1;
        } else if (wellFormed == 0) {
            status = 1;
        }
        if (unreadable > 0 && !s->status)
            fprintf(stderr, "%s: WARNING: %ld listed file%s could not be read\n", currentProgram, unreadable, unreadable == 1 ? "" : "s");
        if (mismatched > 0 && !s->status)
            fprintf(stderr, "%s: WARNING: %ld computed checksum%s did NOT match\n", currentProgram, mismatched, mismatched == 1 ? "" : "s");
        if (bad > 0 && !s->status && (s->warn || 1))
            fprintf(stderr, "%s: WARNING: %ld line%s %s improperly formatted\n", currentProgram, bad, bad == 1 ? "" : "s", bad == 1 ? "is" : "are");
        if (s->ignoreMissing && wellFormed > 0 && verified == 0 && mismatched == 0 && unreadable == 0 && missing > 0 && !s->status) {
            fprintf(stderr, "%s: %s: no file was verified\n", currentProgram, operands[i]);
            status = 1;
        }
        if (unreadable > 0 || mismatched > 0 || (s->strict && bad > 0))
            status = 1;
    }
    return status;
}

int checksum_main(const char* program, const char* version, const char* tag, HashAlgorithm algorithm, int argc, char* argv[]) {
    ArgsSpec spec;
    Settings settings;
    const char** operands;
    int count, result, status;
    static const char* const bitsNames[] = { "MD5 (128-bit)", "SHA1 (160-bit)", "SHA224 (224-bit)", "SHA256 (256-bit)",
                                             "SHA384 (384-bit)", "SHA512 (512-bit)" };

    currentProgram = program;
    currentBits = bitsNames[algorithm];
    memset(&spec, 0, sizeof spec);
    spec.program = program;
    spec.version = version;
    spec.usage = print_usage;
    spec.options = options;
    spec.usageStatus = 1;
    memset(&settings, 0, sizeof settings);
    text_binary_streams();
    result = args_parse(&spec, argc, argv, on_option, &settings, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    if (settings.tag && settings.check) {
        fprintf(stderr, "%s: the --tag option is meaningless when verifying checksums\n", program);
        args_usage_error(&spec);
        return 1;
    }
    if (!settings.check && (settings.warn || settings.status || settings.quiet || settings.strict || settings.ignoreMissing)) {
        const char* what = settings.warn ? "--warn" : settings.status ? "--status" : settings.quiet ? "--quiet" :
                           settings.strict ? "--strict" : "--ignore-missing";

        fprintf(stderr, "%s: the %s option is meaningful only when verifying checksums\n", program, what);
        args_usage_error(&spec);
        return 1;
    }
    if (settings.tag && settings.text) {
        fprintf(stderr, "%s: --tag does not support --text mode\n", program);
        args_usage_error(&spec);
        return 1;
    }
    if (settings.zero && settings.check) {
        fprintf(stderr, "%s: the --zero option is not supported when verifying checksums\n", program);
        args_usage_error(&spec);
        return 1;
    }
    count = (int)text_default_operands(operands, (size_t)count);
    status = settings.check ? check_sums(&settings, tag, algorithm, operands, count)
                            : make_sums(&settings, tag, algorithm, operands, count);
    return fflush(stdout) != 0 ? 1 : status;
}
