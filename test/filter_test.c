/*
 * Tests of sort, uniq, cut and tr.
 */

#include "testtools.h"

/* ---- sort, uniq, cut, tr ------------------------------------------------------------ */
static void test_sort(void) {
    RunResult r;

    printf("sort\n");

    write_text(workDir, "words.txt", "banana\nApple\ncherry\napple\nBanana\ncherry\n");
    write_text(workDir, "nums.txt", "10\n9\n-3\n2.5\n-0.5\n007\n  42\n0\n100\n2.50\n");
    write_text(workDir, "table.txt", "bob:30:sales\nalice:25:dev\ncarol:35:dev\ndave:25:ops\neve:30:sales\n");

    // byte order, as in the C locale: capitals first
    text_expect("sort", "words.txt", NULL, "Apple\nBanana\napple\nbanana\ncherry\ncherry\n");
    text_expect("sort", "-r words.txt", NULL, "cherry\ncherry\nbanana\napple\nBanana\nApple\n");
    text_expect("sort", "-u words.txt", NULL, "Apple\nBanana\napple\nbanana\ncherry\n");
    text_expect("sort", "-f words.txt", NULL, "Apple\napple\nBanana\nbanana\ncherry\ncherry\n");   // equal: last resort
    text_expect("sort", "-fu words.txt", NULL, "Apple\nbanana\ncherry\n");     // of equal lines the first one of the input

    // numbers: by value, not by text
    text_expect("sort", "-n nums.txt", NULL, "-3\n-0.5\n0\n2.5\n2.50\n007\n9\n10\n  42\n100\n");
    text_expect("sort", "-rn nums.txt", NULL, "100\n  42\n10\n9\n007\n2.50\n2.5\n0\n-0.5\n-3\n");
    text_expect("sort", "-nu nums.txt", NULL, "-3\n-0.5\n0\n2.5\n007\n9\n10\n  42\n100\n");
    text_expect("sort", "-g nums.txt", NULL, "-3\n-0.5\n0\n2.5\n2.50\n007\n9\n10\n  42\n100\n");

    // keys and separators
    text_expect("sort", "-t: -k2,2n -k1,1 table.txt", NULL,
                "alice:25:dev\ndave:25:ops\nbob:30:sales\neve:30:sales\ncarol:35:dev\n");
    text_expect("sort", "-t: -k3,3 -k2,2nr table.txt", NULL,
                "carol:35:dev\nalice:25:dev\ndave:25:ops\nbob:30:sales\neve:30:sales\n");
    text_expect("sort", "-t: -k3,3 -u table.txt", NULL, "alice:25:dev\ndave:25:ops\nbob:30:sales\n");
    text_expect("sort", "-k1.2,1.3 words.txt", NULL, "Banana\nbanana\ncherry\ncherry\nApple\napple\n");
    // a field starts with the blanks in front of it, so the longer run of blanks comes first ...
    text_expect("sort", "-k2", "x  b\ny a\nz c\n", "x  b\ny a\nz c\n");
    // ... unless they are ignored
    text_expect("sort", "-k2 -b", "x  b\ny a\nz c\n", "y a\nx  b\nz c\n");

    // stable: equal keys stay in the order of the input
    text_expect("sort", "-s -t: -k2,2n table.txt", NULL,
                "alice:25:dev\ndave:25:ops\nbob:30:sales\neve:30:sales\ncarol:35:dev\n");
    text_expect("sort", "-s -t: -k3,3", "c:1:x\na:2:x\nb:3:x\n", "c:1:x\na:2:x\nb:3:x\n");

    // standard input, a last line without a line feed, several files
    text_expect("sort", "", "b\na\nc", "a\nb\nc\n");
    text_expect("sort", "nums.txt nums.txt -u -n", NULL, "-3\n-0.5\n0\n2.5\n007\n9\n10\n  42\n100\n");
    text_expect("sort", "words.txt words.txt -u", NULL, "Apple\nBanana\napple\nbanana\ncherry\n");

    // CR LF: the ending is not part of the line, and is written back
    text_expect("sort", "", "b\r\na\r\nc\r\n", "a\r\nb\r\nc\r\n");
    text_expect("sort", "-u", "a\r\nb\r\na\r\n", "a\r\nb\r\n");
    text_expect("sort", "-r", "a\r\nb\r\nc\r\n", "c\r\nb\r\na\r\n");
    text_expect("sort", "-n", "10\r\n9\r\n", "9\r\n10\r\n");
    text_expect("sort", "-t: -k2", "x:b\r\ny:a\r\n", "y:a\r\nx:b\r\n");
    // lines of both kinds of endings sort as the same text
    text_expect("sort", "", "b\na\r\nb\r\na\n", "a\r\na\nb\nb\r\n");

    // checking, and writing to a file
    run_tool(workDir, "sort", "-c words.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "sort: words.txt:2: disorder: Apple") != NULL);
    run_result_free(&r);
    write_text(workDir, "sorted.txt", "a\nb\nc\n");
    run_tool(workDir, "sort", "-c sorted.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);
    run_tool(workDir, "sort", "-o sort-out.txt words.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK_STR(r.out, "");
    expect_content(workDir, "sort-out.txt", "Apple\nBanana\napple\nbanana\ncherry\ncherry\n");
    run_result_free(&r);

    // the file itself as output
    write_text(workDir, "inplace.txt", "c\nb\na\n");
    run_tool(workDir, "sort", "-o inplace.txt inplace.txt", NULL, &r);
    expect_content(workDir, "inplace.txt", "a\nb\nc\n");
    run_result_free(&r);

    run_tool(workDir, "sort", "nosuch.txt", NULL, &r);
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "sort: nosuch.txt: No such file or directory") != NULL);
    run_result_free(&r);

    run_tool(workDir, "sort", "-z words.txt", NULL, &r);
    CHECK_INT(r.exitCode, 2);
    CHECK(strstr(r.err, "not supported") != NULL);
    run_result_free(&r);

    run_tool(workDir, "sort", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: sort") != NULL);
    run_result_free(&r);
}

static void test_uniq(void) {
    RunResult r;

    printf("uniq\n");

    text_expect("uniq", "", "a\na\nb\nB\nb\nc\nc\nc\nd\n", "a\nb\nB\nb\nc\nd\n");
    text_expect("uniq", "-c", "a\na\nb\nc\nc\nc\n", "      2 a\n      1 b\n      3 c\n");
    text_expect("uniq", "-d", "a\na\nb\nc\nc\nc\n", "a\nc\n");
    text_expect("uniq", "-D", "a\na\nb\nc\nc\nc\n", "a\na\nc\nc\nc\n");
    text_expect("uniq", "-u", "a\na\nb\nc\nc\nc\nd\n", "b\nd\n");
    text_expect("uniq", "-i", "a\nA\nb\n", "a\nb\n");
    text_expect("uniq", "-ci", "a\nA\nb\n", "      2 a\n      1 b\n");
    text_expect("uniq", "-f 1", "f1 x\nf2 x\nf3 y\n", "f1 x\nf3 y\n");
    text_expect("uniq", "-s 1", "ax\nbx\ncy\n", "ax\ncy\n");
    text_expect("uniq", "-w 1", "ax\nay\nbz\n", "ax\nbz\n");
    text_expect("uniq", "-d -c", "a\na\nb\n", "      2 a\n");
    text_expect("uniq", "", "x", "x\n");                         // a last line without a line feed
    text_expect("uniq", "", "", "");

    // only adjacent lines are merged
    text_expect("uniq", "", "a\nb\na\n", "a\nb\na\n");

    // CR LF: the same line, and the first ending is kept
    text_expect("uniq", "", "a\r\na\r\nb\r\n", "a\r\nb\r\n");
    text_expect("uniq", "", "a\r\na\nb\n", "a\r\nb\n");
    text_expect("uniq", "-c", "a\r\na\r\n", "      2 a\r\n");

    // an input file and an output file
    write_text(workDir, "uin.txt", "x\nx\ny\n");
    run_tool(workDir, "uniq", "uin.txt uout.txt", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    expect_content(workDir, "uout.txt", "x\ny\n");
    run_result_free(&r);

    run_tool(workDir, "uniq", "-D -c", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
    run_tool(workDir, "uniq", "nosuch.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "uniq: nosuch.txt: No such file or directory") != NULL);
    run_result_free(&r);
}

static void test_cut(void) {
    RunResult r;

    printf("cut\n");

    text_expect("cut", "-c1", "abc\ndef\n", "a\nd\n");
    text_expect("cut", "-c2-3", "abcd\nefgh\n", "bc\nfg\n");
    text_expect("cut", "-c2-", "abcd\n", "bcd\n");
    text_expect("cut", "-c-2", "abcd\n", "ab\n");
    text_expect("cut", "-c1,3", "abcd\n", "ac\n");
    text_expect("cut", "-c3,1", "abcd\n", "ac\n");               // always in the order of the line
    text_expect("cut", "-b1-2,4", "abcde\n", "abd\n");
    text_expect("cut", "-c2 --complement", "abc\n", "ac\n");
    text_expect("cut", "-c1-2,4-5 --output-delimiter=|", "abcdef\n", "ab|de\n");
    text_expect("cut", "-c1,2 --output-delimiter=-", "abc\n", "a-b\n");     // two ranges, even next to each other

    // fields: tab is the default, and a line without a delimiter stays
    text_expect("cut", "-f2", "a\tb\tc\n", "b\n");
    text_expect("cut", "-f1,3", "a\tb\tc\n", "a\tc\n");
    text_expect("cut", "-f2-", "a\tb\tc\n", "b\tc\n");
    text_expect("cut", "-f2", "nodelim\n", "nodelim\n");
    text_expect("cut", "-f2 -s", "nodelim\na\tb\n", "b\n");
    text_expect("cut", "-d: -f1", "u:p:x\n", "u\n");
    text_expect("cut", "-d, -f2,3", "1,2,3,4\n", "2,3\n");
    text_expect("cut", "-d: -f1,3 --output-delimiter=@", "a:b:c\n", "a@c\n");
    text_expect("cut", "-d: -f2 --complement", "a:b:c\n", "a:c\n");
    text_expect("cut", "-d: -f4", "a:b:c\n", "\n");               // no such field: an empty line
    text_expect("cut", "-d: -f2", "a::c\n", "\n");                // an empty field
    text_expect("cut", "-f1", "a\tb", "a\n");                    // a last line without a line feed

    // UTF-8 characters, not bytes
    text_expect("cut", "-c1-2", "\xc3\xa9t\xc3\xa9\n", "\xc3\xa9t\n");
    text_expect("cut", "-b1-2", "\xc3\xa9t\xc3\xa9\n", "\xc3\xa9\n");

    // CR LF: the CR is not in the last field, and stays at the end of the line
    text_expect("cut", "-d: -f2", "a:b\r\nc:d\r\n", "b\r\nd\r\n");
    text_expect("cut", "-d: -f1", "a:b\r\n", "a\r\n");
    text_expect("cut", "-c1", "ab\r\ncd\r\n", "a\r\nc\r\n");
    text_expect("cut", "-d: -f3", "a:b\r\n", "\r\n");
    text_expect("cut", "-d: -f2 -s", "a\r\nb:c\r\n", "c\r\n");

    // files
    write_text(workDir, "cutin.txt", "x:y\n");
    text_expect("cut", "-d: -f2 cutin.txt", NULL, "y\n");
    text_expect("cut", "-d: -f2 cutin.txt -", "p:q\n", "y\nq\n");

    run_tool(workDir, "cut", "", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "you must specify a list of bytes, characters, or fields") != NULL);
    run_result_free(&r);
    run_tool(workDir, "cut", "-f0", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    run_result_free(&r);
    run_tool(workDir, "cut", "-c1 -f1", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "only one type of list may be specified") != NULL);
    run_result_free(&r);
    run_tool(workDir, "cut", "-d ab -f1", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "the delimiter must be a single character") != NULL);
    run_result_free(&r);
    run_tool(workDir, "cut", "-f1 nosuch.txt", NULL, &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "cut: nosuch.txt: No such file or directory") != NULL);
    run_result_free(&r);
}

static void test_tr(void) {
    RunResult r;

    printf("tr\n");

    text_expect("tr", "a-z A-Z", "Hello, World\n", "HELLO, WORLD\n");
    text_expect("tr", "[:lower:] [:upper:]", "Hello\n", "HELLO\n");
    text_expect("tr", "[:upper:] [:lower:]", "Hello\n", "hello\n");
    text_expect("tr", "abc xyz", "aabbcc\n", "xxyyzz\n");
    text_expect("tr", "abc x", "aabbcc\n", "xxxxxx\n");            // the last character is repeated
    text_expect("tr", "-t abcd xy", "abcd\n", "xycd\n");
    text_expect("tr", "a-c [x*]", "abc\n", "xxx\n");
    text_expect("tr", "abc [x*2]", "abc\n", "xxx\n");

    // deleting and squeezing
    text_expect("tr", "-d a-c", "abcdef\n", "def\n");
    text_expect("tr", "-d [:digit:]", "a1b22c\n", "abc\n");
    text_expect("tr", "-d [:space:]", "a b\tc\n", "abc");
    text_expect("tr", "-s a-z", "aabbccdd  ee\n", "abcd  e\n");
    text_expect("tr", "-s \" \"", "a   b  c\n", "a b c\n");
    text_expect("tr", "-cd a-z", "a1b2c3\n", "abc");
    text_expect("tr", "-c a-z _", "ab1c\n", "ab_c_");
    text_expect("tr", "-cs a-z \\n", "hello, world.\n", "hello\nworld\n");
    text_expect("tr", "-ds a-c d", "aabbccddee\n", "dee\n");
    text_expect("tr", "-s a-b A-B", "aabb\n", "AB\n");

    // escapes
    text_expect("tr", "\\n \" \"", "a\nb\n", "a b ");
    text_expect("tr", "\" \" \\n", "a b\n", "a\nb\n");
    text_expect("tr", "\\t ,", "a\tb\n", "a,b\n");
    text_expect("tr", "\\101 b", "AAA\n", "bbb\n");

    // line endings are bytes like any other: tr -d '\r' turns CR LF into LF
    text_expect("tr", "-d \\r", "a\r\nb\r\n", "a\nb\n");
    text_expect("tr", "\\n \" \"", "a\r\nb\r\n", "a\r b\r ");
    text_expect("tr", "-d [:space:]", "a b\r\n", "ab");

    // all bytes pass through
    text_expect("tr", "x y", "\x01\x02\xff\xfe\n", "\x01\x02\xff\xfe\n");

    // errors
    run_tool(workDir, "tr", "", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "tr: missing operand") != NULL);
    run_result_free(&r);
    run_tool(workDir, "tr", "a", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "Two strings must be given when translating") != NULL);
    run_result_free(&r);
    run_tool(workDir, "tr", "-d a b", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "extra operand 'b'") != NULL);
    run_result_free(&r);
    run_tool(workDir, "tr", "z-a x", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "reverse collating sequence order") != NULL);
    run_result_free(&r);
    run_tool(workDir, "tr", "[:foo:] x", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "invalid character class") != NULL);
    run_result_free(&r);
    run_tool(workDir, "tr", "[a*] b", "a\n", &r);
    CHECK_INT(r.exitCode, 1);
    CHECK(strstr(r.err, "may not appear in string1") != NULL);
    run_result_free(&r);
    run_tool(workDir, "tr", "--help", NULL, &r);
    CHECK(strstr(r.out, "Usage: tr") != NULL);
    run_result_free(&r);
}

int main(void) {
    tools_init("filter_test");

    test_sort();
    test_uniq();
    test_cut();
    test_tr();
    return test_summary("filter_test");
}
