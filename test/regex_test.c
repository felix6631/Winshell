/*
 * Unit tests of the regular expression engine (common/regex.c): the syntax of basic and extended
 * expressions, the leftmost-longest rule, anchors, classes, back references, UTF-8 and errors.
 */

#include "testlib.h"
#include "regex.h"

static RegexInput input;

/**
 * Searches text (UTF-8) with one pattern. Returns 1 and the character indexes of the match, or 0.
 * A pattern that does not compile returns -1.
 */
static int search(const char* pattern, int flags, const char* text, size_t from, size_t* start, size_t* end) {
    const char* patterns[1];
    Regex* re;
    int found;

    patterns[0] = pattern;
    re = regex_compile(patterns, 1, flags, NULL, 0);
    if (re == NULL)
        return -1;
    regex_input_set(&input, (const unsigned char*)text, strlen(text));
    found = regex_search(re, &input, from, start, end);
    regex_free(re);
    return found;
}

#define EXPECT(flags, pattern, text, s, e) expect_match(__FILE__, __LINE__, flags, pattern, text, s, e)
#define EXPECT_NONE(flags, pattern, text) expect_match(__FILE__, __LINE__, flags, pattern, text, -1, -1)

static void expect_match(const char* file, int line, int flags, const char* pattern, const char* text,
                         int wantStart, int wantEnd) {
    size_t start = 0, end = 0;
    int found = search(pattern, flags, text, 0, &start, &end);

    g_checks++;
    if (wantStart < 0) {
        if (found != 0) {
            g_failures++;
            printf("  FAIL %s:%d: /%s/ matched \"%s\" at [%d,%d), expected no match\n", file, line, pattern, text,
                   (int)start, (int)end);
        }
        return;
    }
    if (found != 1 || (int)start != wantStart || (int)end != wantEnd) {
        g_failures++;
        printf("  FAIL %s:%d: /%s/ on \"%s\": expected [%d,%d), got %s [%d,%d)\n", file, line, pattern, text,
               wantStart, wantEnd, found == 1 ? "match" : (found == 0 ? "no match" : "an error"), (int)start, (int)end);
    }
}

static void expect_error(const char* pattern, int flags, const char* message) {
    const char* patterns[1];
    char error[128];
    Regex* re;

    patterns[0] = pattern;
    error[0] = '\0';
    re = regex_compile(patterns, 1, flags, error, sizeof error);
    g_checks++;
    if (re != NULL) {
        g_failures++;
        printf("  FAIL: /%s/ compiled, expected \"%s\"\n", pattern, message);
        regex_free(re);
    } else if (strcmp(error, message) != 0) {
        g_failures++;
        printf("  FAIL: /%s/: expected \"%s\", got \"%s\"\n", pattern, message, error);
    }
}

#define E REGEX_EXTENDED

static void test_basic_syntax(void) {
    printf("basic syntax\n");

    EXPECT(0, "abc", "xxabcxx", 2, 5);
    EXPECT(0, "a.c", "abc", 0, 3);
    EXPECT_NONE(0, "a.c", "ac");
    EXPECT(0, "ab*c", "ac", 0, 2);
    EXPECT(0, "ab*c", "abbbc", 0, 5);
    EXPECT(0, "a\\+", "caaat", 1, 4);
    EXPECT(0, "ab\\?c", "ac", 0, 2);
    EXPECT(0, "\\(ab\\)*c", "ababc", 0, 5);
    EXPECT(0, "a\\{2\\}", "aaaa", 0, 2);
    EXPECT(0, "a\\{2,\\}", "aaaa", 0, 4);
    EXPECT(0, "a\\{1,2\\}", "aaaa", 0, 2);
    EXPECT(0, "cat\\|dog", "hotdog", 3, 6);

    // in basic syntax these are characters
    EXPECT(0, "a+b", "a+b", 0, 3);
    EXPECT(0, "a?b", "a?b", 0, 3);
    EXPECT(0, "(a)", "(a)", 0, 3);
    EXPECT(0, "a|b", "a|b", 0, 3);
    EXPECT(0, "a{2}", "a{2}", 0, 4);

    // a * with nothing to repeat is a character
    EXPECT(0, "*a", "x*a", 1, 3);
    EXPECT(0, "^*a", "*a", 0, 2);
    EXPECT(0, "\\(*a\\)", "*a", 0, 2);

    // ^ and $ are anchors at the ends of an expression only
    EXPECT(0, "^abc$", "abc", 0, 3);
    EXPECT_NONE(0, "^abc$", "xabc");
    EXPECT(0, "a^b", "a^b", 0, 3);
    EXPECT(0, "a$b", "a$b", 0, 3);
    EXPECT(0, "\\(^a\\)", "ab", 0, 1);
    EXPECT(0, "x\\|^a", "ab", 0, 1);
    EXPECT(0, "a$\\|x", "ba", 1, 2);
}

static void test_extended_syntax(void) {
    printf("extended syntax\n");

    EXPECT(E, "a+", "caaat", 1, 4);
    EXPECT(E, "ab?c", "ac", 0, 2);
    EXPECT(E, "(ab)+c", "ababc", 0, 5);
    EXPECT(E, "a{2}", "aaaa", 0, 2);
    EXPECT(E, "a{2,3}", "aaaa", 0, 3);
    EXPECT(E, "a{,2}b", "aaab", 1, 4);
    EXPECT(E, "cat|dog", "hotdog", 3, 6);
    EXPECT(E, "a|b|c", "xxc", 2, 3);
    EXPECT(E, "()", "abc", 0, 0);
    EXPECT(E, ")", "a)", 1, 2);                 // an unmatched ) is a character
    EXPECT(E, "a)", "xa)", 1, 3);
    EXPECT(E, "a||b", "b", 0, 1);       // the empty alternative loses to the longer one
    EXPECT(E, "a||b", "c", 0, 0);

    // a "{" that starts no interval is a character
    EXPECT(E, "a{", "a{", 0, 2);
    EXPECT(E, "a{x}", "a{x}", 0, 4);
    EXPECT(E, "{", "x{", 1, 2);
    EXPECT(E, "{1}x", "{1}x", 3, 4);                // an interval of nothing is nothing
    EXPECT(E, "*a", "*a", 1, 2);                    // and so is a * with nothing to repeat
    EXPECT(E, "+a", "+a", 1, 2);
    EXPECT(E, "a|*b", "*b", 1, 2);

    // escapes
    EXPECT(E, "a\\+", "a+", 0, 2);
    EXPECT(E, "\\(a\\)", "(a)", 0, 3);
    EXPECT(E, "a\\.b", "a.b", 0, 3);
    EXPECT_NONE(E, "a\\.b", "axb");

    // anchors anywhere
    EXPECT(E, "a|^b", "xb", -1, -1);
    EXPECT(E, "(^a)", "ab", 0, 1);
    EXPECT(E, "a$|b", "ba", 0, 1);
}

static void test_longest(void) {
    printf("leftmost, longest\n");

    EXPECT(E, "a|ab", "abc", 0, 2);
    EXPECT(E, "ab|a", "abc", 0, 2);
    EXPECT(E, "(a|ab)(c|bcd)", "abcd", 0, 4);
    EXPECT(E, "a*", "baaa", 0, 0);
    EXPECT(E, "b*", "abbb", 0, 0);
    EXPECT(E, "x*|b+", "abbb", 0, 0);
    EXPECT(E, "[ab]+|b", "abbb", 0, 4);
    EXPECT(E, "(a*)(ab)*b", "aabb", 0, 4);
    EXPECT(E, "o+|oo+x", "fooox", 1, 5);
    EXPECT(E, "abc|b", "xabcd", 1, 4);
    EXPECT(E, "b|abc", "xabcd", 1, 4);
}

static void test_sets(void) {
    printf("bracket expressions and classes\n");

    EXPECT(0, "[abc]", "xxb", 2, 3);
    EXPECT(0, "[^abc]", "abcd", 3, 4);
    EXPECT(0, "[a-c]*", "abcd", 0, 3);
    EXPECT(0, "[]a]", "]", 0, 1);
    EXPECT(0, "[^]a]", "]ab", 2, 3);
    EXPECT(0, "[a-]", "-", 0, 1);
    EXPECT(0, "[-a]", "-", 0, 1);
    EXPECT(0, "[.]", "a.", 1, 2);
    EXPECT(0, "[\\]", "\\", 0, 1);              // a backslash is ordinary in a bracket
    EXPECT(0, "[[:digit:]]\\+", "ab123c", 2, 5);
    EXPECT(0, "[[:alpha:]]\\+", "12ab3", 2, 4);
    EXPECT(0, "[[:upper:]][[:lower:]]", "xAbY", 1, 3);
    EXPECT(0, "[[:space:]]", "a b", 1, 2);
    EXPECT(0, "[[:punct:]]", "ab,c", 2, 3);
    EXPECT(0, "[[:alnum:]_]\\+", "--a_1--", 2, 5);
    EXPECT(0, "[^[:space:]]\\+", "  ab  ", 2, 4);
    EXPECT(0, "[[:xdigit:]]\\+", "xyz1fAq", 3, 6);
    EXPECT(0, "[[.a.]]", "a", 0, 1);
    EXPECT(0, "[[=a=]]", "a", 0, 1);
    EXPECT(0, "[a-c[:digit:]]\\+", "x1a2z", 1, 4);
    EXPECT_NONE(0, "[^a]", "a");

    EXPECT(0, "\\w\\+", "  ab_1  ", 2, 6);
    EXPECT(0, "\\W", "ab c", 2, 3);
    EXPECT(0, "\\s", "a\tb", 1, 2);
    EXPECT(0, "\\S\\+", "  ab ", 2, 4);
}

static void test_word_and_line(void) {
    printf("word boundaries, whole words and lines\n");

    EXPECT(0, "\\bfoo\\b", "a foo b", 2, 5);
    EXPECT_NONE(0, "\\bfoo\\b", "afoob");
    EXPECT(0, "\\Boo", "foo", 1, 3);
    EXPECT(0, "\\<foo", "x foo", 2, 5);
    EXPECT_NONE(0, "\\<foo", "xfoo");
    EXPECT(0, "foo\\>", "foo bar", 0, 3);
    EXPECT_NONE(0, "foo\\>", "foobar");
    EXPECT(0, "\\`a", "ab", 0, 1);
    EXPECT(0, "b\\'", "ab", 1, 2);

    // -w: the match has to be a whole word, and a shorter match at the same place is tried
    EXPECT(REGEX_WORD, "foo", "a foobar foo", 9, 12);
    EXPECT(REGEX_WORD, "foo.*", "foo bar foo", 0, 11);
    EXPECT(REGEX_WORD, "a.*", "a b ab", 0, 6);
    EXPECT_NONE(REGEX_WORD, "foo", "foobar");
    EXPECT(REGEX_WORD, "o", "foo o", 4, 5);
    EXPECT(REGEX_WORD, "foo-bar", "x foo-bar y", 2, 9);

    // -x
    EXPECT(REGEX_LINE, "abc", "abc", 0, 3);
    EXPECT_NONE(REGEX_LINE, "abc", "abcd");
    EXPECT(REGEX_LINE | E, "a|ab", "ab", 0, 2);
    EXPECT(REGEX_LINE, "", "", 0, 0);
    EXPECT_NONE(REGEX_LINE, "", "a");
}

static void test_backreferences(void) {
    printf("back references\n");

    EXPECT(0, "\\(a*\\)b\\1", "aabaa", 0, 5);
    EXPECT(0, "\\(a*\\)b\\1", "aaba", 1, 4);
    EXPECT(0, "\\(.\\)\\1", "abccd", 2, 4);
    EXPECT(E, "(.)(.)\\2\\1", "xabbay", 1, 5);
    EXPECT(E, "^(.+)\\1$", "abcabc", 0, 6);
    EXPECT_NONE(E, "^(.+)\\1$", "abcab");
    EXPECT(E, "(a|b)\\1", "xabbx", 2, 4);
    EXPECT(E, "(a)|b\\1", "a", 0, 1);
    EXPECT(REGEX_EXTENDED | REGEX_ICASE, "(a)\\1", "aA", 0, 2);

    // two patterns number their groups on their own
    {
        const char* patterns[2] = { "(a)\\1", "(b)\\1" };
        Regex* re = regex_compile(patterns, 2, REGEX_EXTENDED, NULL, 0);
        size_t s = 0, e = 0;
        g_checks += 2;
        if (re == NULL) {
            g_failures++;
            printf("  FAIL: two patterns with back references did not compile\n");
            return;
        }
        regex_input_set(&input, (const unsigned char*)"xbb", 3);
        if (!regex_search(re, &input, 0, &s, &e) || s != 1 || e != 3) {
            g_failures++;
            printf("  FAIL: (a)\\1 or (b)\\1 on xbb: got [%d,%d)\n", (int)s, (int)e);
        }
        regex_free(re);
    }
}

static void test_case_and_text(void) {
    printf("case, UTF-8 and where the search starts\n");

    EXPECT(REGEX_ICASE, "hello", "say HeLLo", 4, 9);
    EXPECT(REGEX_ICASE, "[a-c]+", "xBCAx", -1, -1);
    EXPECT(REGEX_ICASE | E, "[a-c]+", "xBCAx", 1, 4);
    EXPECT(REGEX_ICASE | E, "[^a]", "Ab", 1, 2);
    EXPECT(REGEX_ICASE | REGEX_FIXED, "A.C", "xa.cx", 1, 4);

    // fixed strings
    EXPECT(REGEX_FIXED, "a.c*", "abc a.c*", 4, 8);
    EXPECT(REGEX_FIXED, "[x]", "a[x]", 1, 4);
    EXPECT(REGEX_FIXED, "", "abc", 0, 0);

    // a character is a whole UTF-8 sequence
    EXPECT(0, ".", "\xEA\xB0\x80", 0, 1);                      // 가
    EXPECT(0, "^.$", "\xEA\xB0\x80", 0, 1);
    EXPECT(0, "^..$", "\xEA\xB0\x80", -1, -1);
    EXPECT(0, "[\xEA\xB0\x80-\xED\x9E\xA3]", "x\xEB\x82\x98", 1, 2);   // [가-힣] and 나
    EXPECT(0, "\xC3\xA9", "caf\xC3\xA9", 3, 4);                // é
    EXPECT(REGEX_ICASE, "\xC3\xA9", "CAF\xC3\x89", 3, 4);      // é and É
    EXPECT(0, "\\w\\+", "\xEA\xB0\x80\xEB\x82\x98 x", 0, 2);

    // a byte that is no UTF-8 matches only itself
    EXPECT_NONE(0, "^.$", "\xFF");
    EXPECT(0, "\xFF", "a\xFF", 1, 2);
    EXPECT_NONE(0, "[^a]", "\xFF");

    // a search that starts later
    {
        size_t s, e;
        g_checks += 2;
        if (search("a", 0, "aaa", 1, &s, &e) != 1 || s != 1 || e != 2) {
            g_failures++;
            printf("  FAIL: search from 1\n");
        }
        if (search("^a", 0, "aaa", 1, &s, &e) != 0) {
            g_failures++;
            printf("  FAIL: ^ matched after the start\n");
        }
    }
}

static void test_errors(void) {
    printf("errors\n");

    expect_error("\\(", 0, "Unmatched ( or \\(");
    expect_error("(", E, "Unmatched ( or \\(");
    expect_error("\\)", 0, "Unmatched ) or \\)");
    expect_error("(+)", E, "Invalid preceding regular expression");
    expect_error("(a", E, "Unmatched ( or \\(");
    expect_error("[a", 0, "Unmatched [, [^, [:, [., or [=");
    expect_error("[", 0, "Unmatched [, [^, [:, [., or [=");
    expect_error("a\\", 0, "Trailing backslash");
    expect_error("[[:foo:]]", 0, "Invalid character class name");
    expect_error("[:alpha:]", 0, "character class syntax is [[:space:]], not [:space:]");
    expect_error("[b-a]", 0, "Invalid range end");
    expect_error("\\1", 0, "Invalid back reference");
    expect_error("\\(a\\)\\2", 0, "Invalid back reference");
    expect_error("a\\{2,1\\}", 0, "Invalid content of \\{\\}");
    expect_error("a\\{1", 0, "Unmatched \\{");
    expect_error("a{2,1}", E, "Invalid content of \\{\\}");
    expect_error("a{99999}", E, "Regular expression too big");
    expect_error("\\{1\\}", 0, "Invalid preceding regular expression");
}

static void test_many(void) {
    const char* patterns[3] = { "foo", "ba+r", "^x" };
    Regex* re;
    size_t s, e;

    printf("several patterns\n");

    re = regex_compile(patterns, 3, E, NULL, 0);
    g_checks++;
    if (re == NULL) {
        g_failures++;
        printf("  FAIL: three patterns did not compile\n");
        return;
    }
    regex_input_set(&input, (const unsigned char*)"a baaar foo", 11);
    g_checks += 2;
    if (!regex_search(re, &input, 0, &s, &e) || s != 2 || e != 7) {
        g_failures++;
        printf("  FAIL: the first match of the patterns: [%d,%d)\n", (int)s, (int)e);
    }
    if (!regex_search(re, &input, 7, &s, &e) || s != 8 || e != 11) {
        g_failures++;
        printf("  FAIL: the next match: [%d,%d)\n", (int)s, (int)e);
    }
    regex_free(re);

    // no pattern at all matches nothing
    re = regex_compile(NULL, 0, 0, NULL, 0);
    g_checks++;
    if (re == NULL || regex_search(re, &input, 0, &s, &e)) {
        g_failures++;
        printf("  FAIL: an empty list of patterns matched\n");
    }
    regex_free(re);
}

int main(void) {
    test_basic_syntax();
    test_extended_syntax();
    test_longest();
    test_sets();
    test_word_and_line();
    test_backreferences();
    test_case_and_text();
    test_errors();
    test_many();
    regex_input_free(&input);
    return test_summary("regex_test");
}
