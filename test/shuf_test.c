/*
 * Tests of shuf, run as the program it is: with a random source the result is the one of GNU shuf, without it only what
 * chance cannot change is checked (every line once, the count, the lines of a file or the numbers of a range).
 * GNU shuf is compared with by gnu_compare.sh (the same random bytes, many shapes).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("shuf", arguments, input, expected, status);
}

/** The lines of the output, sorted, joined by a space: what a permutation keeps. */
static void sorted_words(const char* text, char* out, size_t size) {
    char copy[4096], *words[256], *p, *context = NULL;
    int n = 0, i, j;

    snprintf(copy, sizeof copy, "%s", text);
    for (p = strtok_s(copy, "\r\n", &context); p != NULL && n < 256; p = strtok_s(NULL, "\r\n", &context))
        words[n++] = p;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && strcmp(words[j - 1], words[j]) > 0; j--) {
            char* t = words[j];

            words[j] = words[j - 1];
            words[j - 1] = t;
        }
    out[0] = '\0';
    for (i = 0; i < n; i++)
        snprintf(out + strlen(out), size - strlen(out), "%s%s", i > 0 ? " " : "", words[i]);
}

static int line_count(const char* text) {
    int n = 0;

    for (; *text != '\0'; text++)
        if (*text == '\n')
            n++;
    return n;
}

int main(void) {
    RunResult r;
    char sorted[4096];
    int i;

    tools_init("shuf_test");
    reset_dir(workDir);
    write_text(workDir, "src.txt", "The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. "
               "The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. ");
    write_text(workDir, "in.txt", "l1\nl2\nl3\nl4\nl5");

    printf("a random source gives the permutation of GNU shuf\n");
    expect("--random-source=src.txt -i 1-10", "", "5\n10\n3\n2\n6\n7\n1\n4\n8\n9\n", 0);
    expect("--random-source=src.txt -e a b c d e", "", "e\nb\nd\na\nc\n", 0);
    expect("--random-source=src.txt -i 1-100 -n 5", "", "85\n7\n66\n28\n54\n", 0);
    expect("--random-source=src.txt -r -i 1-6 -n 8", "", "1\n3\n3\n3\n6\n3\n6\n5\n", 0);
    expect("--random-source=src.txt in.txt", "", "l5\nl2\nl4\nl1\nl3\n", 0);        // the last line gets its line feed
    expect("--random-source=src.txt in.txt -n 0", "", "", 0);
    expect("--random-source=nosuchfile -i 1-3", "", "", 1);
    run_tool(workDir, "shuf", "--random-source=src.txt -i 1-100000", "", &r);       // the bytes run out: nothing is written
    CHECK_INT(r.exitCode, 1);
    CHECK_STR(r.out, "");
    CHECK(strstr(r.err, "end of file") != NULL);
    run_result_free(&r);

    printf("a permutation keeps what it is made of\n");
    run_tool(workDir, "shuf", "-i 1-20", "", &r);
    sorted_words(r.out, sorted, sizeof sorted);
    CHECK_STR(sorted, "1 10 11 12 13 14 15 16 17 18 19 2 20 3 4 5 6 7 8 9");
    run_result_free(&r);
    run_tool(workDir, "shuf", "in.txt", "", &r);
    sorted_words(r.out, sorted, sizeof sorted);
    CHECK_STR(sorted, "l1 l2 l3 l4 l5");
    CHECK(r.out[strlen(r.out) - 1] == '\n');
    run_result_free(&r);
    run_tool(workDir, "shuf", "-e x y z", "", &r);
    sorted_words(r.out, sorted, sizeof sorted);
    CHECK_STR(sorted, "x y z");
    run_result_free(&r);
    run_tool(workDir, "shuf", "", "p\r\nq\r\nr\r\n", &r);                         // CR LF lines come back as they were
    CHECK_INT((int)strlen(r.out), 9);
    CHECK(strstr(r.out, "p\r\n") != NULL && strstr(r.out, "q\r\n") != NULL && strstr(r.out, "r\r\n") != NULL);
    run_result_free(&r);
    run_tool(workDir, "shuf", "-i 1-50 -n 7", "", &r);
    CHECK_INT(line_count(r.out), 7);
    run_result_free(&r);
    run_tool(workDir, "shuf", "-i 1-5 -n 99", "", &r);
    CHECK_INT(line_count(r.out), 5);
    run_result_free(&r);
    run_tool(workDir, "shuf", "-r -n 30 -e same", "", &r);
    CHECK_INT(line_count(r.out), 30);
    CHECK_INT((int)strlen(r.out), 150);
    run_result_free(&r);
    run_tool(workDir, "shuf", "-zi 1-3", "", &r);
    CHECK_INT((int)r.outLength, 6);
    run_result_free(&r);
    for (i = 0; i < 3; i++) {
        run_tool(workDir, "shuf", "-i 1-1000000000000000 -n 3", "", &r);             // a huge range is not made
        CHECK_INT(line_count(r.out), 3);
        run_result_free(&r);
    }

    printf("-o and the arguments\n");
    expect("-o out.txt in.txt", "", "", 0);
    CHECK(path_exists_in(workDir, "out.txt"));
    expect("-i 3-3", "", "3\n", 0);
    expect("-i 3-2", "", "", 0);
    expect("-e", "", "", 0);
    run_tool(workDir, "shuf", "-n 5 -n 1 -e only more", "", &r);                   // the smaller count wins
    CHECK_INT(line_count(r.out), 1);
    run_result_free(&r);

    printf("errors\n");
    expect("-i 5-3", "", "", 1);
    expect("-i x-3", "", "", 1);
    expect("-i 1-3 -i 2-4", "", "", 1);
    expect("-n x", "", "", 1);
    expect("-n -1", "", "", 1);
    expect("-e -i 1-3", "", "", 1);
    expect("-i 1-3 extra", "", "", 1);
    expect("a b", "", "", 1);
    expect("-r", "", "", 1);                                                      // nothing to repeat
    expect("-r -n 0", "", "", 0);
    expect("nosuchfile", "", "", 1);
    expect("--bogus", "", "", 1);
    expect("--version", "", "shuf (Winshell) 0.1\n", 0);

    return test_summary("shuf_test");
}
