/*
 * A property test of the rule that LF and CR LF line endings are the same
 * to the text tools: for random texts, the output for the text with CR LF
 * endings, and for a text with both kinds mixed, is the output for the text
 * with LF endings, with the endings kept as they were:
 *
 *   tool(crlf)           == crlf_of(tool(lf))
 *   strip_cr(tool(mixed)) == tool(lf)
 *
 * and the same for texts whose last line has no line ending. The tools are
 * cat, sort, uniq, cut, head, tail and wc, each with options picked at
 * random. tr is left out: it works on bytes, and a CR is a byte like any other.
 *
 * Environment: WS_ITER number of random texts (default 25), WS_SEED the seed.
 * A failure prints the seed, the tool and its options, and the input.
 */

#include "testtools.h"

static unsigned long long rngState;

static unsigned rnd(unsigned bound) {
    rngState = rngState * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned)((rngState >> 33) % bound);
}

#define MAX_LINES 24
#define MAX_LINE 9

typedef struct {
    char text[MAX_LINE + 1];
} Line;

/** Random lines from a small alphabet, so that equal lines, blanks, numbers and fields all occur. */
static int make_lines(Line* lines) {
    static const char alphabet[] = "aabbcE 1250\t:-.3 ";
    int count = (int)rnd(MAX_LINES + 1), i;

    for (i = 0; i < count; i++) {
        unsigned length = rnd(MAX_LINE), k;
        for (k = 0; k < length; k++)
            lines[i].text[k] = alphabet[rnd(sizeof alphabet - 1)];
        lines[i].text[length] = '\0';

        // equal neighbours are what uniq and sort -u are about
        if (i > 0 && rnd(4) == 0)
            strcpy(lines[i].text, lines[i - 1].text);
    }
    return count;
}

/** kind: 0 LF, 1 CR LF, 2 mixed; the last line ends without a line ending when finalEnding is 0. */
static char* build(const Line* lines, int count, int kind, const unsigned char* mixed, int finalEnding, size_t* length) {
    char* text = malloc((size_t)count * (MAX_LINE + 3) + 1);
    size_t n = 0;
    int i;

    for (i = 0; i < count; i++) {
        size_t l = strlen(lines[i].text);
        memcpy(text + n, lines[i].text, l);
        n += l;
        if (i == count - 1 && !finalEnding)
            break;
        if (kind == 1 || (kind == 2 && mixed[i]))
            text[n++] = '\r';
        text[n++] = '\n';
    }
    text[n] = '\0';
    *length = n;
    return text;
}

static void show(const char* text, size_t length) {
    size_t i;
    putchar('"');
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\n')
            printf("\\n");
        else if (c == '\r')
            printf("\\r");
        else if (c == '\t')
            printf("\\t");
        else
            putchar(c);
    }
    putchar('"');
}

/** Replaces every LF by CR LF. */
static char* crlf_of(const char* text, size_t length, size_t* outLength) {
    char* out = malloc(length * 2 + 1);
    size_t i, n = 0;
    for (i = 0; i < length; i++) {
        if (text[i] == '\n')
            out[n++] = '\r';
        out[n++] = text[i];
    }
    out[n] = '\0';
    *outLength = n;
    return out;
}

/** Removes every CR. */
static char* strip_cr(const char* text, size_t length, size_t* outLength) {
    char* out = malloc(length + 1);
    size_t i, n = 0;
    for (i = 0; i < length; i++)
        if (text[i] != '\r')
            out[n++] = text[i];
    out[n] = '\0';
    *outLength = n;
    return out;
}

static int run_text(const char* tool, const char* options, const char* input, size_t inputLength,
                    char** out, size_t* outLength) {
    char path[MAX_PATH * 2];
    RunResult r;

    bin_path(tool, path, sizeof path);
    if (run_program_ex(path, options, input, inputLength, NULL, workDir, 20000, &r) != 0)
        return -1;
    *out = r.out;
    *outLength = r.outLength;
    free(r.err);

    // wc pads its numbers to the size of its input, which is larger with CR LF: the numbers are what is compared
    if (strcmp(tool, "wc") == 0) {
        char *read = r.out, *write = r.out;
        int atLineStart = 1, previousBlank = 0;
        for (; *read != '\0'; read++) {
            if (*read == ' ') {
                if (!atLineStart && !previousBlank)
                    *write++ = ' ';
                previousBlank = 1;
                continue;
            }
            previousBlank = 0;
            atLineStart = (*read == '\n');
            *write++ = *read;
        }
        *write = '\0';
        *outLength = (size_t)(write - r.out);
    }
    return 0;
}

static unsigned long long seedOfRun;
static int iteration;

static void fail(const char* property, const char* tool, const char* options,
                 const char* input, size_t inputLength, const char* expected, size_t expectedLength,
                 const char* actual, size_t actualLength) {
    g_failures++;
    printf("  FAIL %s: %s %s (seed %llu, text %d)\n    input:    ", property, tool, options, seedOfRun, iteration);
    show(input, inputLength);
    printf("\n    expected: ");
    show(expected, expectedLength);
    printf("\n    actual:   ");
    show(actual, actualLength);
    printf("\n");
}

typedef struct {
    const char* tool;
    const char** options;
    int count;
    int comparesCr;             // the output has line endings: they have to be CR LF for CR LF input
} ToolSet;

static const char* catOptions[] = { "", "-n", "-b", "-s", "-E", "-ns", "-bE", "-sE" };
static const char* sortOptions[] = { "", "-r", "-u", "-n", "-f", "-b", "-k2", "-t: -k2", "-nr", "-u -f", "-k1,1 -s",
                                     "-k2,2n -t:", "-d", "-ru" };
static const char* uniqOptions[] = { "", "-c", "-d", "-u", "-i", "-f1", "-s1", "-D", "-ci" };
static const char* cutOptions[] = { "-c1-3", "-c2", "-b1,3", "-f2", "-d: -f1", "-d: -f2-", "-d: -f2 -s",
                                    "--complement -c1", "-c3-", "-d\" \" -f1,2" };
static const char* headOptions[] = { "-n3", "-n 0", "-n -2", "-n 100", "-n1" };
static const char* tailOptions[] = { "-n3", "-n +3", "-n 100", "-n 0", "-n1", "-n +1" };
static const char* wcOptions[] = { "-l", "-w", "-L", "-lwL" };

#define SET(tool, array, cr) { tool, array, (int)(sizeof array / sizeof array[0]), cr }

static const ToolSet toolSets[] = {
    SET("cat", catOptions, 1),
    SET("sort", sortOptions, 1),
    SET("uniq", uniqOptions, 1),
    SET("cut", cutOptions, 1),
    SET("head", headOptions, 1),
    SET("tail", tailOptions, 1),
    SET("wc", wcOptions, 0),
};

/** Checks the properties of one tool with one set of options for one random text. */
static void check_tool(const ToolSet* set, const char* options, const Line* lines, int count) {
    unsigned char mixed[MAX_LINES];
    int finalEnding, i;

    for (i = 0; i < MAX_LINES; i++)
        mixed[i] = (unsigned char)(rnd(2));

    for (finalEnding = 1; finalEnding >= 0; finalEnding--) {
        size_t lfLength, crlfLength, mixedLength;
        char* lf = build(lines, count, 0, mixed, finalEnding, &lfLength);
        char* crlf = build(lines, count, 1, mixed, finalEnding, &crlfLength);
        char* mix = build(lines, count, 2, mixed, finalEnding, &mixedLength);
        char *outLf = NULL, *outCrlf = NULL, *outMixed = NULL;
        size_t outLfLength = 0, outCrlfLength = 0, outMixedLength = 0;

        g_checks += 2;
        if (run_text(set->tool, options, lf, lfLength, &outLf, &outLfLength) != 0 ||
            run_text(set->tool, options, crlf, crlfLength, &outCrlf, &outCrlfLength) != 0 ||
            run_text(set->tool, options, mix, mixedLength, &outMixed, &outMixedLength) != 0) {
            g_failures++;
            printf("  FAIL: %s could not be run\n", set->tool);
            return;
        }

        // 1. CR LF in, CR LF out: the same output with every line feed made a CR LF.
        //    (A last line without ending is given an LF, which is no CR LF, so only with all endings.)
        if (set->comparesCr && finalEnding) {
            size_t expectedLength;
            char* expected = crlf_of(outLf, outLfLength, &expectedLength);
            if (expectedLength != outCrlfLength || memcmp(expected, outCrlf, expectedLength) != 0)
                fail("CR LF input", set->tool, options, crlf, crlfLength, expected, expectedLength, outCrlf, outCrlfLength);
            free(expected);
        }

        // 2. Mixed endings in: without the CRs, the output for LF.
        {
            size_t strippedLength;
            char* stripped = strip_cr(outMixed, outMixedLength, &strippedLength);
            if (strippedLength != outLfLength || memcmp(stripped, outLf, outLfLength) != 0)
                fail("mixed endings", set->tool, options, mix, mixedLength, outLf, outLfLength, stripped, strippedLength);
            free(stripped);
        }

        // 3. CR LF in: without the CRs, the output for LF (this is what holds for wc as well).
        {
            size_t strippedLength;
            char* stripped = strip_cr(outCrlf, outCrlfLength, &strippedLength);
            if (strippedLength != outLfLength || memcmp(stripped, outLf, outLfLength) != 0)
                fail("CR LF without CR", set->tool, options, crlf, crlfLength, outLf, outLfLength, stripped, strippedLength);
            free(stripped);
        }

        free(lf);
        free(crlf);
        free(mix);
        free(outLf);
        free(outCrlf);
        free(outMixed);
    }
}

int main(void) {
    const char* iterations = getenv("WS_ITER");
    const char* seed = getenv("WS_SEED");
    int count = iterations != NULL ? atoi(iterations) : 25, i;
    unsigned long long base = seed != NULL ? (unsigned long long)_strtoui64(seed, NULL, 10) : 20260101ULL;

    tools_init("equiv_test");
    printf("LF and CR LF: %d random texts, seed %llu\n", count, base);

    for (i = 0; i < count; i++) {
        Line lines[MAX_LINES];
        int lineCount, t;

        iteration = i;
        seedOfRun = base;
        rngState = base * 1000003ULL + (unsigned long long)i;
        lineCount = make_lines(lines);

        for (t = 0; t < (int)(sizeof toolSets / sizeof toolSets[0]); t++) {
            const ToolSet* set = &toolSets[t];
            const char* chosen = set->options[rnd((unsigned)set->count)];
            check_tool(set, chosen, lines, lineCount);
        }
    }

    // the cases that are easy to get wrong, always: no lines, one empty line, only line endings
    {
        static const char* fixed[] = { "", "\n", "\r\n", "\n\n\n", "\r\n\r\n", "a", "a\r\n", "\r\n\r\na", "a\n\r\nb\r\n" };
        static const char* fixedTools[] = { "cat -b", "cat -s", "sort", "sort -u", "uniq -c", "uniq -d", "head -n 1",
                                            "tail -n 1", "wc -l", "cut -c1" };
        size_t a, b;
        for (a = 0; a < sizeof fixed / sizeof fixed[0]; a++) {
            for (b = 0; b < sizeof fixedTools / sizeof fixedTools[0]; b++) {
                char tool[16], options[32];
                const char* space = strchr(fixedTools[b], ' ');
                char* outText = NULL;
                size_t outLength = 0, strippedLength, textLength = strlen(fixed[a]);
                char *stripped, *plainInput, *plainOut = NULL;
                size_t plainOutLength = 0, strippedInputLength, toolLength;

                // "tool options", or only "tool"
                toolLength = space != NULL ? (size_t)(space - fixedTools[b]) : strlen(fixedTools[b]);
                memcpy(tool, fixedTools[b], toolLength);
                tool[toolLength] = '\0';
                snprintf(options, sizeof options, "%s", space != NULL ? space + 1 : "");

                // the same text without its CRs is the LF version of it
                plainInput = strip_cr(fixed[a], textLength, &strippedInputLength);
                g_checks++;
                if (run_text(tool, options, fixed[a], textLength, &outText, &outLength) != 0 ||
                    run_text(tool, options, plainInput, strippedInputLength, &plainOut, &plainOutLength) != 0) {
                    g_failures++;
                    continue;
                }
                stripped = strip_cr(outText, outLength, &strippedLength);
                if (strippedLength != plainOutLength || memcmp(stripped, plainOut, plainOutLength) != 0)
                    fail("fixed text", tool, options, fixed[a], textLength, plainOut, plainOutLength, stripped, strippedLength);
                free(stripped);
                free(plainInput);
                free(outText);
                free(plainOut);
            }
        }
    }

    return test_summary("equiv_test");
}
