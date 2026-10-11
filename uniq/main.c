#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "uniq"

static void print_usage(void) {
    puts("Usage: uniq [OPTION]... [INPUT [OUTPUT]]\n"
         "Filter adjacent matching lines from INPUT (or standard input),\n"
         "writing to OUTPUT (or standard output).\n"
         "\n"
         "With no options, matching lines are merged to the first occurrence.\n"
         "\n"
         "  -c, --count           prefix lines by the number of occurrences\n"
         "  -d, --repeated        only print duplicate lines, one for each group\n"
         "  -D                    print all duplicate lines\n"
         "  -f, --skip-fields=N   avoid comparing the first N fields\n"
         "  -i, --ignore-case     ignore differences in case when comparing\n"
         "  -s, --skip-chars=N    avoid comparing the first N characters\n"
         "  -u, --unique          only print unique lines\n"
         "  -w, --check-chars=N   compare no more than N characters in lines\n"
         "      --help            display this help and exit\n"
         "      --version         output version information and exit\n"
         "\n"
         "A field is a run of blanks (usually spaces and/or TABs), then non-blank\n"
         "characters. Fields are skipped before chars.\n"
         "A CR in front of the line feed belongs to the line ending, not to the line.");
}

static void try_help(void) {
    fprintf(stderr, "Try '" PROGRAM " --help' for more information.\n");
}

static int count_option, repeated, all_repeated, unique_only, ignore_case;
static unsigned long skip_fields, skip_chars, check_chars = (unsigned long)-1;

/** The part of the line that is compared. */
static void compared_part(const TextLine* line, const unsigned char** start, size_t* length) {
    const unsigned char* p = line->content;
    const unsigned char* end = line->content + line->length;
    unsigned long n;

    for (n = skip_fields; n > 0 && p < end; n--) {
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;
        while (p < end && *p != ' ' && *p != '\t')
            p++;
    }
    for (n = skip_chars; n > 0 && p < end; n--)
        p++;

    *start = p;
    *length = (size_t)(end - p);
    if (check_chars != (unsigned long)-1 && *length > check_chars)
        *length = check_chars;
}

static int same_lines(const TextLine* a, const TextLine* b) {
    const unsigned char *pa, *pb;
    size_t la, lb;

    compared_part(a, &pa, &la);
    compared_part(b, &pb, &lb);
    if (la != lb)
        return 0;
    if (!ignore_case)
        return memcmp(pa, pb, la) == 0;
    {
        size_t i;
        for (i = 0; i < la; i++)
            if (tolower(pa[i]) != tolower(pb[i]))
                return 0;
    }
    return 1;
}

static int parse_number(const char* text, unsigned long* out, const char* what) {
    char* end;
    unsigned long value = strtoul(text, &end, 10);
    if (text[0] == '\0' || *end != '\0' || text[0] == '-') {
        fprintf(stderr, PROGRAM ": %s: invalid number of %s\n", text, what);
        return -1;
    }
    *out = value;
    return 0;
}

int main(int argc, char* argv[]) {
    const char* operands[2];
    int operandCount = 0, optionsEnded = 0, i;
    FILE *in = stdin, *out = stdout;
    unsigned char* text;
    size_t length = 0, offset = 0;
    TextLine group, line;
    unsigned long groupCount = 0;
    int haveGroup = 0;

    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (optionsEnded || arg[0] != '-' || arg[1] == '\0') {
            if (operandCount == 2) {
                fprintf(stderr, PROGRAM ": extra operand '%s'\n", arg);
                try_help();
                return 1;
            }
            operands[operandCount++] = arg;
        } else if (strcmp(arg, "--") == 0) {
            optionsEnded = 1;
        } else if (arg[1] == '-') {
            const char* value;

            if (strcmp(arg, "--help") == 0) {
                print_usage();
                return 0;
            } else if (strcmp(arg, "--version") == 0) {
                puts(PROGRAM " (Winshell) " VERSION);
                return 0;
            } else if (strcmp(arg, "--count") == 0) {
                count_option = 1;
            } else if (strcmp(arg, "--repeated") == 0) {
                repeated = 1;
            } else if (strcmp(arg, "--unique") == 0) {
                unique_only = 1;
            } else if (strcmp(arg, "--ignore-case") == 0) {
                ignore_case = 1;
            } else if (strncmp(arg, "--skip-fields=", 14) == 0) {
                value = arg + 14;
                if (parse_number(value, &skip_fields, "fields to skip") != 0)
                    return 1;
            } else if (strncmp(arg, "--skip-chars=", 13) == 0) {
                value = arg + 13;
                if (parse_number(value, &skip_chars, "bytes to skip") != 0)
                    return 1;
            } else if (strncmp(arg, "--check-chars=", 14) == 0) {
                value = arg + 14;
                if (parse_number(value, &check_chars, "bytes to compare") != 0)
                    return 1;
            } else {
                fprintf(stderr, PROGRAM ": unrecognized option '%s'\n", arg);
                try_help();
                return 1;
            }
        } else {
            const char* p;
            // -N means -f N
            if (arg[1] >= '0' && arg[1] <= '9') {
                if (parse_number(arg + 1, &skip_fields, "fields to skip") != 0)
                    return 1;
                continue;
            }
            for (p = arg + 1; *p != '\0'; p++) {
                switch (*p) {
                    case 'c': count_option = 1; break;
                    case 'd': repeated = 1; break;
                    case 'D': all_repeated = 1; break;
                    case 'u': unique_only = 1; break;
                    case 'i': ignore_case = 1; break;
                    case 'f':
                    case 's':
                    case 'w': {
                        char option = *p;
                        const char* value;
                        unsigned long* target = option == 'f' ? &skip_fields : (option == 's' ? &skip_chars : &check_chars);

                        if (p[1] != '\0') {
                            value = p + 1;
                        } else if (i + 1 < argc) {
                            value = argv[++i];
                        } else {
                            fprintf(stderr, PROGRAM ": option requires an argument -- '%c'\n", option);
                            try_help();
                            return 1;
                        }
                        if (parse_number(value, target, option == 'f' ? "fields to skip" :
                                                         (option == 's' ? "bytes to skip" : "bytes to compare")) != 0)
                            return 1;
                        p += strlen(p) - 1;
                        break;
                    }
                    default:
                        fprintf(stderr, PROGRAM ": invalid option -- '%c'\n", *p);
                        try_help();
                        return 1;
                }
            }
        }
    }

    if (all_repeated && count_option) {
        fprintf(stderr, PROGRAM ": printing all duplicated lines and repeat counts is meaningless\n");
        try_help();
        return 1;
    }

    text_binary_streams();

    if (operandCount >= 1 && strcmp(operands[0], "-") != 0 && text_open(PROGRAM, operands[0], 1, &in) != 0)
        return 1;
    if (operandCount == 2 && strcmp(operands[1], "-") != 0) {
        char native[VPATH_MAX];
        if (vpath_to_native(operands[1], native, sizeof native) != 0 || (out = fopen(native, "wb")) == NULL) {
            fprintf(stderr, PROGRAM ": %s: No such file or directory\n", operands[1]);
            return 1;
        }
    }

    text = text_slurp(in, &length);
    if (in != stdin)
        fclose(in);
    if (text == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return 1;
    }

    // Lines of a group are written when the group ends. With -D all of them are written as they come.
    for (;;) {
        int more = text_next_line(text, length, &offset, &line);
        int sameGroup = more && haveGroup && same_lines(&group, &line);

        if (haveGroup && !sameGroup) {
            int show = all_repeated ? 0 : (repeated ? groupCount > 1 : (unique_only ? groupCount == 1 : 1));
            if (show) {
                if (count_option)
                    fprintf(out, "%7lu ", groupCount);
                text_write_line(&group, out);
            }
        }

        if (!more)
            break;

        if (sameGroup) {
            groupCount++;
            if (all_repeated) {
                if (groupCount == 2)
                    text_write_line(&group, out);       // the first one of the group, now known to be repeated
                text_write_line(&line, out);
            }
        } else {
            group = line;
            groupCount = 1;
            haveGroup = 1;
        }
    }

    if (out != stdout)
        fclose(out);
    else
        fflush(stdout);
    free(text);
    return 0;
}
