/*
 * Tests of cat: bytes pass through unchanged, the options, CR LF as one line ending.
 */

#include "testtools.h"

static void write_fixtures(void) {
    static const unsigned char allBytes[256] = {
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
        32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
        64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,
        96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,
        121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,
        145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,
        169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,
        193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,
        217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,
        241,242,243,244,245,246,247,248,249,250,251,252,253,254,255 };

    write_file(workDir, "all.bin", allBytes, sizeof allBytes);
    write_text(workDir, "lf.txt", "a\n\n\n\tb\nc");              // no newline at the end
    write_text(workDir, "crlf.txt", "a\r\n\r\n\r\n\tb\r\nc\r\n");
    write_text(workDir, "cr.txt", "\rx\r");                      // lone CRs
    write_text(workDir, "two.txt", "two\n");
    write_text(workDir, "high.txt", "\x01\x7f\xc3\xa9\xff");     // control, DEL, and bytes >= 128
}

/** Runs cat with arguments and compares the exact bytes written. */
static void cat_expect(const char* arguments, const char* input, const char* expected, size_t expectedLength) {
    RunResult r;
    run_command("cat", arguments, input, &r);
    CHECK(r.out != NULL && strlen(r.out) == expectedLength && memcmp(r.out, expected, expectedLength) == 0);
    if (r.out == NULL || strlen(r.out) != expectedLength || memcmp(r.out, expected, expectedLength) != 0)
        printf("    for: cat %s\n    got: \"%s\"\n", arguments, r.out == NULL ? "(null)" : r.out);
    run_result_free(&r);
}

#define CAT_EXPECT(arguments, input, expected) cat_expect(arguments, input, expected, sizeof expected - 1)

static void test_cat(void) {
    RunResult r;
    size_t length;
    char* original;

    printf("cat\n");
    write_fixtures();

    // bytes pass through unchanged: every value, and CR LF as it is
    run_command("cat", "all.bin", NULL, &r);
    original = read_file(workDir, "all.bin", &length);
    CHECK_INT(length, 256);
    CHECK(original != NULL && r.out != NULL && memcmp(original, r.out, 256) == 0);
    free(original);
    run_result_free(&r);

    CAT_EXPECT("crlf.txt", NULL, "a\r\n\r\n\r\n\tb\r\nc\r\n");
    CAT_EXPECT("lf.txt two.txt", NULL, "a\n\n\n\tb\nctwo\n");

    // standard input, and "-" in the middle
    CAT_EXPECT("", "from stdin\r\n", "from stdin\r\n");
    CAT_EXPECT("lf.txt - two.txt", "<stdin>", "a\n\n\n\tb\nc<stdin>two\n");

    // numbering
    CAT_EXPECT("-n lf.txt", NULL, "     1\ta\n     2\t\n     3\t\n     4\t\tb\n     5\tc");
    CAT_EXPECT("-b lf.txt", NULL, "     1\ta\n\n\n     2\t\tb\n     3\tc");
    CAT_EXPECT("-b -n lf.txt", NULL, "     1\ta\n\n\n     2\t\tb\n     3\tc");    // -b wins
    CAT_EXPECT("-s lf.txt", NULL, "a\n\n\tb\nc");
    CAT_EXPECT("-ns lf.txt", NULL, "     1\ta\n     2\t\n     3\t\tb\n     4\tc");

    // numbers continue over files, like one stream
    CAT_EXPECT("-n two.txt two.txt", NULL, "     1\ttwo\n     2\ttwo\n");

    // visible ends and tabs
    CAT_EXPECT("-E lf.txt", NULL, "a$\n$\n$\n\tb$\nc");
    CAT_EXPECT("-T lf.txt", NULL, "a\n\n\n^Ib\nc");
    CAT_EXPECT("-A lf.txt", NULL, "a$\n$\n$\n^Ib$\nc");
    CAT_EXPECT("-e lf.txt", NULL, "a$\n$\n$\n\tb$\nc");
    CAT_EXPECT("-t lf.txt", NULL, "a\n\n\n^Ib\nc");

    // non printing characters
    CAT_EXPECT("-v high.txt", NULL, "^A^?M-CM-)M-^?");

    // CR LF is one line ending: it counts as blank, and $ goes in front of it
    CAT_EXPECT("-b crlf.txt", NULL, "     1\ta\r\n\r\n\r\n     2\t\tb\r\n     3\tc\r\n");
    CAT_EXPECT("-s crlf.txt", NULL, "a\r\n\r\n\tb\r\nc\r\n");
    CAT_EXPECT("-E crlf.txt", NULL, "a$\r\n$\r\n$\r\n\tb$\r\nc$\r\n");
    // ... and with -v the CR is shown as GNU cat does
    CAT_EXPECT("-A crlf.txt", NULL, "a^M$\n^M$\n^M$\n^Ib^M$\nc^M$\n");
    CAT_EXPECT("-v crlf.txt", NULL, "a^M\n^M\n^M\n\tb^M\nc^M\n");

    // CRs that are not part of a CR LF are ordinary characters
    CAT_EXPECT("-A cr.txt", NULL, "^Mx^M");
    CAT_EXPECT("-E cr.txt", NULL, "\rx\r");

    // errors: the other files are still shown, the exit code tells
    run_command("cat", "nosuch.txt two.txt", NULL, &r);
    CHECK_STR(lf_only(r.out), "two\n");
    CHECK(strstr(r.err, "cat: nosuch.txt: No such file or directory") != NULL);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    run_command("cat", "/", NULL, &r);
    CHECK(strstr(r.err, "cat: /: Is a directory") != NULL);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);

    // a directory of the work directory, in virtual and in Windows notation
    {
        char arguments[MAX_PATH * 2];
        snprintf(arguments, sizeof arguments, "\"%s\"", workDir);
        run_command("cat", arguments, NULL, &r);
        CHECK(strstr(r.err, "Is a directory") != NULL);
        CHECK_INT(r.exitCode, 1);
        run_result_free(&r);

        snprintf(arguments, sizeof arguments, "\"%s/two.txt\"", virtualWorkDir);
        run_command("cat", arguments, NULL, &r);
        CHECK_STR(lf_only(r.out), "two\n");
        CHECK_INT(r.exitCode, 0);
        run_result_free(&r);

        snprintf(arguments, sizeof arguments, "\"%s\\two.txt\"", workDir);
        run_command("cat", arguments, NULL, &r);
        CHECK_STR(lf_only(r.out), "two\n");
        run_result_free(&r);
    }

    run_command("cat", "-x", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "invalid option -- 'x'") != NULL);
    run_result_free(&r);

    run_command("cat", "--bogus", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "unrecognized option '--bogus'") != NULL);
    run_result_free(&r);

    // "--" ends the options
    CAT_EXPECT("-- two.txt", NULL, "two\n");

    run_command("cat", "--version", NULL, &r);
    CHECK(strstr(r.out, "cat") != NULL);
    run_result_free(&r);

    run_command("cat", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: cat") != NULL);
    run_result_free(&r);
}

int main(void) {
    tools_init("cat_test");

    test_cat();
    return test_summary("cat_test");
}
