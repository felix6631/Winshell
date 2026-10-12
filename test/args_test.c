/*
 * Unit tests of the option parser of the tools (common/args.c).
 */

#include "testlib.h"
#include "args.h"

enum { OPT_A = 1, OPT_B, OPT_FILE, OPT_COLOR, OPT_VERBOSE, OPT_VERSION_LIKE };

static const ArgsOption options[] = {
    { OPT_A,       'a', "all",        ARGS_NONE },
    { OPT_B,       'b', "bytes",      ARGS_NONE },
    { OPT_FILE,    'f', "file",       ARGS_REQUIRED },
    { OPT_COLOR,   0,   "color",      ARGS_OPTIONAL },
    { OPT_VERBOSE, 'v', "verbose",    ARGS_NONE },
    { OPT_VERSION_LIKE, 0, "verify",  ARGS_NONE },
    { 0 }
};

static int usageCalls = 0;
static void usage(void) { usageCalls++; }

static const ArgsSpec spec = { "tool", "9.9", usage, options, 3, 0, 0 };
static const ArgsSpec stopping = { "tool", "9.9", usage, options, 3, 1, 0 };
static const ArgsSpec numbered = { "tool", "9.9", usage, options, 3, 0, 1 };

typedef struct {
    char log[256];
} Seen;

static int record(int id, const char* value, void* context) {
    Seen* seen = context;
    char entry[64];

    snprintf(entry, sizeof entry, "%d%s%s;", id, value != NULL ? "=" : "", value != NULL ? value : "");
    strcat(seen->log, entry);
    return 0;
}

static int refuse(int id, const char* value, void* context) {
    (void)id; (void)value; (void)context;
    return -1;
}

/** Parses the words (without the program name); returns the result, and the log and operands. */
static int parse(const ArgsSpec* s, const char* words, Seen* seen, char* operandsText, size_t size, ArgsHandler handler) {
    char* argv[32];
    char copy[256];
    int argc = 1, count = 0, result, i;
    const char** operands = NULL;
    char* token;

    snprintf(copy, sizeof copy, "%s", words);
    argv[0] = "tool";
    for (token = strtok(copy, " "); token != NULL && argc < 31; token = strtok(NULL, " "))
        argv[argc++] = token;
    argv[argc] = NULL;

    seen->log[0] = '\0';
    result = args_parse(s, argc, argv, handler != NULL ? handler : record, seen, &operands, &count);
    operandsText[0] = '\0';
    if (result == ARGS_OK) {
        for (i = 0; i < count; i++) {
            strcat(operandsText, operands[i]);
            strcat(operandsText, i + 1 < count ? "," : "");
        }
    }
    return result;
}

static void silence_stderr(void) {
    fflush(stderr);
    freopen("NUL", "w", stderr);
}

static void test_options(void) {
    Seen seen;
    char operands[128];

    printf("options\n");

    CHECK_INT(parse(&spec, "", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "");
    CHECK_STR(operands, "");

    // short, long, grouped, and operands in between
    CHECK_INT(parse(&spec, "-a x --bytes y", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "1;2;");
    CHECK_STR(operands, "x,y");
    CHECK_INT(parse(&spec, "-ab", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "1;2;");
    CHECK_INT(parse(&spec, "-abv", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "1;2;5;");

    // values: the rest of the word, the next word, --name=value, --name value
    CHECK_INT(parse(&spec, "-fname", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "3=name;");
    CHECK_INT(parse(&spec, "-f name", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "3=name;");
    CHECK_INT(parse(&spec, "--file=name", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "3=name;");
    CHECK_INT(parse(&spec, "--file name", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "3=name;");
    CHECK_INT(parse(&spec, "-af name -b", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "1;3=name;2;");
    // a value may look like an option
    CHECK_INT(parse(&spec, "-f -a", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "3=-a;");

    // an optional value only with '='
    CHECK_INT(parse(&spec, "--color", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "4;");
    CHECK_INT(parse(&spec, "--color=always x", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "4=always;");
    CHECK_STR(operands, "x");

    // abbreviations of long options
    CHECK_INT(parse(&spec, "--by", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "2;");
    CHECK_INT(parse(&spec, "--verb", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "5;");

    // "--" and "-" are operands
    CHECK_INT(parse(&spec, "-a -- -b --all", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "1;");
    CHECK_STR(operands, "-b,--all");
    CHECK_INT(parse(&spec, "- -a", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(operands, "-");
}

static void test_modes(void) {
    Seen seen;
    char operands[128];

    printf("modes\n");

    // the first operand ends the options (env)
    CHECK_INT(parse(&stopping, "-a x -b --all", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "1;");
    CHECK_STR(operands, "x,-b,--all");

    // numbers as options (head -5)
    CHECK_INT(parse(&numbered, "-5 x", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "-1=5;");
    CHECK_STR(operands, "x");
    CHECK_INT(parse(&numbered, "-12 -a", &seen, operands, sizeof operands, NULL), ARGS_OK);
    CHECK_STR(seen.log, "-1=12;1;");
    // ... which are plain bad options when the tool does not want them
    silence_stderr();
    CHECK_INT(parse(&spec, "-5", &seen, operands, sizeof operands, NULL), ARGS_FAILED);
}

static void test_results(void) {
    Seen seen;
    char operands[128];

    printf("help, version, errors\n");

    usageCalls = 0;
    CHECK_INT(parse(&spec, "--help", &seen, operands, sizeof operands, NULL), ARGS_EXIT);
    CHECK_INT(usageCalls, 1);
    CHECK_INT(ARGS_STATUS(spec, ARGS_EXIT), 0);
    CHECK_INT(parse(&spec, "-a --version", &seen, operands, sizeof operands, NULL), ARGS_EXIT);
    CHECK_INT(parse(&spec, "--hel", &seen, operands, sizeof operands, NULL), ARGS_FAILED);   // not a prefix of anything that exists
    CHECK_INT(ARGS_STATUS(spec, ARGS_FAILED), 3);

    silence_stderr();
    CHECK_INT(parse(&spec, "-z", &seen, operands, sizeof operands, NULL), ARGS_FAILED);
    CHECK_INT(parse(&spec, "-az", &seen, operands, sizeof operands, NULL), ARGS_FAILED);
    CHECK_INT(parse(&spec, "--nosuch", &seen, operands, sizeof operands, NULL), ARGS_FAILED);
    CHECK_INT(parse(&spec, "--ver", &seen, operands, sizeof operands, NULL), ARGS_FAILED);       // verbose or verify
    CHECK_INT(parse(&spec, "-f", &seen, operands, sizeof operands, NULL), ARGS_FAILED);          // value missing
    CHECK_INT(parse(&spec, "--file", &seen, operands, sizeof operands, NULL), ARGS_FAILED);
    CHECK_INT(parse(&spec, "--all=1", &seen, operands, sizeof operands, NULL), ARGS_FAILED);     // no value allowed

    // a handler that refuses fails the parse
    CHECK_INT(parse(&spec, "-a", &seen, operands, sizeof operands, refuse), ARGS_FAILED);
}

int main(void) {
    test_options();
    test_modes();
    test_results();
    return test_summary("args_test");
}
