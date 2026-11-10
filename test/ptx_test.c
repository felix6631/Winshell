/*
 * Tests of ptx, run as the program it is: the plain, roff and TeX formats, the fields (the text before and after the keyword
 * and what goes to the other side), references (-A, -r, -R), widths and gaps, the truncation mark, ignore and only lists, case
 * folding, break characters, word and sentence expressions, the traditional mode, and the errors. The expected texts are the
 * ones GNU ptx writes; gnu_compare.sh compares with it in several hundred more combinations.
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("ptx", arguments, input, expected, status);
}

static void message(const char* arguments, const char* input, const char* part, int status) {
    RunResult r;

    run_tool(workDir, "ptx", arguments, input, &r);
    CHECK_INT(r.exitCode, status);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

int main(void) {
    tools_init("ptx_test");
    reset_dir(workDir);
    write_text(workDir, "a.txt", "The quick brown fox jumps over the lazy dog.\nA second line, with other words: foxes and dogs!\n");
    write_text(workDir, "b.txt", "abc_def 123 x9y it's a-b\n");
    write_text(workDir, "c.txt", "one. two  three.  four\nfive?\n");
    write_text(workDir, "d.txt", "the Cat the cat THE CAT a b A B\n");
    write_text(workDir, "r.txt", "ref1 the cat sat\nref22 a dog ran\n");
    write_text(workDir, "s.txt", "say \"hi\" {x} $5 & 100% #1 _u a\\b\n");
    write_text(workDir, "t.txt", "a\tb  c\n");
    write_text(workDir, "ign.txt", "the\na\nand\n");
    write_text(workDir, "only.txt", "cat\nB\n");
    write_text(workDir, "brk.txt", "-_");
    write_text(workDir, "l.txt", "Supercalifragilisticexpialidocious is long. Short ones: a b c.\n");

    // ptx a.txt
    expect("a.txt",
           "", "   foxes and dogs!                     A second line, with other words:\n"
           "   lazy dog.                           The quick brown fox jumps over the\n"
           "       line, with other words: foxes   and dogs!                   A second\n"
           "                           The quick   brown fox jumps over the lazy dog.\n"
           "       brown fox jumps over the lazy   dog.                       The quick\n"
           "       , with other words: foxes and   dogs!                   /second line\n"
           "                     The quick brown   fox jumps over the lazy dog.\n"
           "      second line, with other words:   foxes and dogs!                    A\n"
           "                 The quick brown fox   jumps over the lazy dog.\n"
           "      quick brown fox jumps over the   lazy dog.                        The\n"
           "   dogs!                    A second   line, with other words: foxes and\n"
           "                 A second line, with   other words: foxes and dogs!\n"
           "           The quick brown fox jumps   over the lazy dog.\n"
           "   lazy dog.                     The   quick brown fox jumps over the\n"
           "   foxes and dogs!                 A   second line, with other words:\n"
           "      The quick brown fox jumps over   the lazy dog.\n                      A second line,   with other words: foxes and dogs!\n"
           "           A second line, with other   words: foxes and dogs!\n",
           0);
    // ptx -w 40 a.txt
    expect("-w 40 a.txt",
           "", "   with other/         A second line,\n   fox jumps/          The quick brown\n"
           "        words: foxes   and dogs!     /other\n           The quick   brown fox jumps/\n"
           "       over the lazy   dog.      /fox jumps\n         : foxes and   dogs!         /words\n"
           "     The quick brown   fox jumps over the/\n       /other words:   foxes and dogs!\n"
           "    /quick brown fox   jumps over the/\n      jumps over the   lazy dog.       /fox\n"
           "            A second   line, with other/\n         /line, with   other words: foxes/\n"
           "    /brown fox jumps   over the lazy dog.\n   jumps/        The   quick brown fox\n"
           "   other/          A   second line, with\n     /fox jumps over   the lazy dog.\n"
           "      A second line,   with other words:/\n       /, with other   words: foxes and/\n",
           0);
    // ptx -A a.txt
    expect("-A a.txt",
           "", "a.txt:2:  words: foxes and dogs!         A second line, with other\n"
           "a.txt:1:  over the lazy dog.             The quick brown fox jumps\n"
           "a.txt:2:     , with other words: foxes   and dogs!                 /line\n"
           "a.txt:1:  dog.               The quick   brown fox jumps over the lazy\n"
           "a.txt:1:       fox jumps over the lazy   dog.               /quick brown\n"
           "a.txt:2:        other words: foxes and   dogs!               /line, with\n"
           "a.txt:1:               The quick brown   fox jumps over the lazy dog.\n"
           "a.txt:2:       line, with other words:   foxes and dogs!        A second\n"
           "a.txt:1:           The quick brown fox   jumps over the lazy dog.\n"
           "a.txt:1:      brown fox jumps over the   lazy dog.             The quick\n"
           "a.txt:2:  and dogs!           A second   line, with other words: foxes\n"
           "a.txt:2:           A second line, with   other words: foxes and dogs!\n"
           "a.txt:1:     The quick brown fox jumps   over the lazy dog.\n"
           "a.txt:1:  the lazy dog.            The   quick brown fox jumps over\n"
           "a.txt:2:  : foxes and dogs!          A   second line, with other words\n"
           "a.txt:1:    quick brown fox jumps over   the lazy dog.               The\n"
           "a.txt:2:  dogs!         A second line,   with other words: foxes and\n"
           "a.txt:2:     A second line, with other   words: foxes and dogs!\n",
           0);
    // ptx -A -R -w 50 a.txt
    expect("-A -R -w 50 a.txt",
           "", "other words:/            A second line, with         a.txt:2\n"
           "jumps over the/          The quick brown fox         a.txt:1\n"
           "    other words: foxes   and dogs!     /line, with   a.txt:2\n"
           "the/         The quick   brown fox jumps over        a.txt:1\n"
           "   jumps over the lazy   dog.           /brown fox   a.txt:1\n"
           "      words: foxes and   dogs!       /, with other   a.txt:2\n"
           "       The quick brown   fox jumps over the lazy/    a.txt:1\n"
           "   , with other words:   foxes and dogs!     /line   a.txt:2\n"
           "   The quick brown fox   jumps over the lazy dog/    a.txt:1\n"
           "    fox jumps over the   lazy dog.          /brown   a.txt:1\n"
           "foxes/        A second   line, with other words:     a.txt:2\n"
           "   A second line, with   other words: foxes and/     a.txt:2\n"
           "      /brown fox jumps   over the lazy dog.          a.txt:1\n"
           "over the lazy/     The   quick brown fox jumps       a.txt:1\n"
           "words: foxes/        A   second line, with other     a.txt:2\n"
           "  brown fox jumps over   the lazy dog.      /quick   a.txt:1\n"
           "        A second line,   with other words: foxes/    a.txt:2\n"
           "     /line, with other   words: foxes and dogs!      a.txt:2\n",
           0);
    // ptx -O a.txt
    expect("-O a.txt",
           "", ".xx \"foxes and dogs!\" \"\" \"A second line, with other words:\" \"\"\n"
           ".xx \"lazy dog.\" \"\" \"The quick brown fox jumps over the\" \"\"\n"
           ".xx \"\" \"line, with other words: foxes\" \"and dogs!\" \"A second\"\n"
           ".xx \"\" \"The quick\" \"brown fox jumps over the lazy dog.\" \"\"\n"
           ".xx \"\" \"brown fox jumps over the lazy\" \"dog.\" \"The quick\"\n"
           ".xx \"\" \", with other words: foxes and\" \"dogs!\" \"/second line\"\n"
           ".xx \"\" \"The quick brown\" \"fox jumps over the lazy dog.\" \"\"\n"
           ".xx \"\" \"second line, with other words:\" \"foxes and dogs!\" \"A\"\n"
           ".xx \"\" \"The quick brown fox\" \"jumps over the lazy dog.\" \"\"\n"
           ".xx \"\" \"quick brown fox jumps over the\" \"lazy dog.\" \"The\"\n"
           ".xx \"dogs!\" \"A second\" \"line, with other words: foxes and\" \"\"\n"
           ".xx \"\" \"A second line, with\" \"other words: foxes and dogs!\" \"\"\n"
           ".xx \"\" \"The quick brown fox jumps\" \"over the lazy dog.\" \"\"\n"
           ".xx \"lazy dog.\" \"The\" \"quick brown fox jumps over the\" \"\"\n"
           ".xx \"foxes and dogs!\" \"A\" \"second line, with other words:\" \"\"\n"
           ".xx \"\" \"The quick brown fox jumps over\" \"the lazy dog.\" \"\"\n"
           ".xx \"\" \"A second line,\" \"with other words: foxes and dogs!\" \"\"\n"
           ".xx \"\" \"A second line, with other\" \"words: foxes and dogs!\" \"\"\n",
           0);
    // ptx -T a.txt
    expect("-T a.txt",
           "", "\\xx {foxes and dogs!}{}{A}{ second line, with other words:}{}\n"
           "\\xx {lazy dog.}{}{The}{ quick brown fox jumps over the}{}\n"
           "\\xx {}{line, with other words: foxes}{and}{ dogs!}{A second}\n"
           "\\xx {}{The quick}{brown}{ fox jumps over the lazy dog.}{}\n"
           "\\xx {}{brown fox jumps over the lazy}{dog}{.}{The quick}\n"
           "\\xx {}{, with other words: foxes and}{dogs}{!}{second line}\n"
           "\\xx {}{The quick brown}{fox}{ jumps over the lazy dog.}{}\n"
           "\\xx {}{second line, with other words:}{foxes}{ and dogs!}{A}\n"
           "\\xx {}{The quick brown fox}{jumps}{ over the lazy dog.}{}\n"
           "\\xx {}{quick brown fox jumps over the}{lazy}{ dog.}{The}\n"
           "\\xx {dogs!}{A second}{line}{, with other words: foxes and}{}\n"
           "\\xx {}{A second line, with}{other}{ words: foxes and dogs!}{}\n"
           "\\xx {}{The quick brown fox jumps}{over}{ the lazy dog.}{}\n"
           "\\xx {lazy dog.}{The}{quick}{ brown fox jumps over the}{}\n"
           "\\xx {foxes and dogs!}{A}{second}{ line, with other words:}{}\n"
           "\\xx {}{The quick brown fox jumps over}{the}{ lazy dog.}{}\n"
           "\\xx {}{A second line,}{with}{ other words: foxes and dogs!}{}\n"
           "\\xx {}{A second line, with other}{words}{: foxes and dogs!}{}\n",
           0);
    // ptx -O -A -w 40 a.txt
    expect("-O -A -w 40 a.txt",
           "", ".xx \", with/\" \"\" \"A second line\" \"\" \"a.txt:2\"\n.xx \"brown/\" \"\" \"The quick\" \"\" \"a.txt:1\"\n"
           ".xx \"\" \"/: foxes\" \"and dogs!\" \"\" \"a.txt:2\"\n.xx \"\" \"The quick\" \"brown fox/\" \"\" \"a.txt:1\"\n"
           ".xx \"\" \"the lazy\" \"dog.\" \"/over\" \"a.txt:1\"\n.xx \"\" \"foxes and\" \"dogs!\" \"/:\" \"a.txt:2\"\n"
           ".xx \"\" \"/brown\" \"fox jumps/\" \"\" \"a.txt:1\"\n.xx \"\" \"/words:\" \"foxes and/\" \"\" \"a.txt:2\"\n"
           ".xx \"\" \"/brown fox\" \"jumps over/\" \"\" \"a.txt:1\"\n.xx \"\" \"/over the\" \"lazy dog.\" \"\" \"a.txt:1\"\n"
           ".xx \"\" \"A second\" \"line, with/\" \"\" \"a.txt:2\"\n.xx \"\" \"/line, with\" \"other words:/\" \"\" \"a.txt:2\"\n"
           ".xx \"\" \"/fox jumps\" \"over the lazy/\" \"\" \"a.txt:1\"\n.xx \"fox/\" \"The\" \"quick brown\" \"\" \"a.txt:1\"\n"
           ".xx \"with/\" \"A\" \"second line,\" \"\" \"a.txt:2\"\n.xx \"\" \"/jumps over\" \"the lazy dog.\" \"\" \"a.txt:1\"\n"
           ".xx \"\" \"/line,\" \"with other/\" \"\" \"a.txt:2\"\n.xx \"\" \"/with other\" \"words: foxes/\" \"\" \"a.txt:2\"\n",
           0);
    // ptx -T -w 40 a.txt
    expect("-T -w 40 a.txt",
           "", "\\xx {with other}{}{A}{ second line,}{}\n\\xx {fox jumps}{}{The}{ quick brown}{}\n"
           "\\xx {}{words: foxes}{and}{ dogs!}{other}\n\\xx {}{The quick}{brown}{ fox jumps}{}\n"
           "\\xx {}{over the lazy}{dog}{.}{fox jumps}\n\\xx {}{: foxes and}{dogs}{!}{words}\n"
           "\\xx {}{The quick brown}{fox}{ jumps over the}{}\n\\xx {}{other words:}{foxes}{ and dogs!}{}\n"
           "\\xx {}{quick brown fox}{jumps}{ over the}{}\n\\xx {}{jumps over the}{lazy}{ dog.}{fox}\n"
           "\\xx {}{A second}{line}{, with other}{}\n\\xx {}{line, with}{other}{ words: foxes}{}\n"
           "\\xx {}{brown fox jumps}{over}{ the lazy dog.}{}\n\\xx {jumps}{The}{quick}{ brown fox}{}\n"
           "\\xx {other}{A}{second}{ line, with}{}\n\\xx {}{fox jumps over}{the}{ lazy dog.}{}\n"
           "\\xx {}{A second line,}{with}{ other words:}{}\n\\xx {}{, with other}{words}{: foxes and}{}\n",
           0);
    // ptx -G a.txt
    expect("-G a.txt",
           "", ".xx \"foxes and dogs!\" \"\" \"A second line, with other words:\" \"\"\n"
           ".xx \"the lazy dog.\" \"\" \"The quick brown fox jumps over\" \"\"\n"
           ".xx \"\" \"line, with other words: foxes\" \"and dogs!\" \"A second\"\n"
           ".xx \"dog.\" \"The quick\" \"brown fox jumps over the lazy\" \"\"\n"
           ".xx \"\" \"brown fox jumps over the lazy\" \"dog.\" \"The quick\"\n"
           ".xx \"\" \"line, with other words: foxes and\" \"dogs!\" \"/second\"\n"
           ".xx \"\" \"The quick brown\" \"fox jumps over the lazy dog.\" \"\"\n"
           ".xx \"\" \"A second line, with other words:\" \"foxes and dogs!\" \"\"\n"
           ".xx \"\" \"The quick brown fox\" \"jumps over the lazy dog.\" \"\"\n"
           ".xx \"\" \"quick brown fox jumps over the\" \"lazy dog.\" \"The\"\n"
           ".xx \"dogs!\" \"A second\" \"line, with other words: foxes and\" \"\"\n"
           ".xx \"\" \"A second line, with\" \"other words: foxes and dogs!\" \"\"\n"
           ".xx \"\" \"The quick brown fox jumps\" \"over the lazy dog.\" \"\"\n"
           ".xx \"lazy dog.\" \"The\" \"quick brown fox jumps over the\" \"\"\n"
           ".xx \"foxes and dogs!\" \"A\" \"second line, with other words:\" \"\"\n"
           ".xx \"\" \"The quick brown fox jumps over\" \"the lazy dog.\" \"\"\n"
           ".xx \"\" \"A second line,\" \"with other words: foxes and dogs!\" \"\"\n"
           ".xx \"\" \"A second line, with other\" \"words: foxes and dogs!\" \"\"\n",
           0);
    // ptx -g 5 -w 50 a.txt
    expect("-g 5 -w 50 a.txt",
           "", "     other words:/            A second line, with\n     jumps over/              The quick brown fox\n"
           "       other words: foxes     and dogs!         /, with\n     the/       The quick     brown fox jumps over\n"
           "            over the lazy     dog.           /fox jumps\n         words: foxes and     dogs!       /, with other\n"
           "          The quick brown     fox jumps over the lazy/\n        with other words:     foxes and dogs!        /,\n"
           "         /quick brown fox     jumps over the lazy dog/\n       fox jumps over the     lazy dog.          /brown\n"
           "                 A second     line, with other words:/\n       /second line, with     other words: foxes and/\n"
           "         /brown fox jumps     over the lazy dog.\n     over the/        The     quick brown fox jumps\n"
           "     words:/            A     second line, with other\n           fox jumps over     the lazy dog.      /brown\n"
           "           A second line,     with other words: foxes/\n        /line, with other     words: foxes and dogs!\n",
           0);
    // ptx -F ... -w 30 a.txt
    expect("-F ... -w 30 a.txt",
           "", "                  A second...\n                  The quick...\n"
           "       ...foxes   and dogs!\n       ...quick   brown fox...\n"
           "        ...lazy   dog.\n         ...and   dogs!\n       ...brown   fox jumps...\n"
           "      ...words:   foxes and...\n         ...fox   jumps...\n"
           "         ...the   lazy dog.\n         second   line,...      A\n"
           "      ..., with   other...\n       ...jumps   over the...\n"
           "            The   quick...\n              A   second...\n        ...over   the lazy...\n"
           "       ...line,   with...\n       ...other   words:...\n",
           0);
    // ptx -F "" -w 30 a.txt
    expect("-F \"\" -w 30 a.txt",
           "", "   with           A second line,\n   fox            The quick brown\n"
           "   words: foxes   and dogs!\n      The quick   brown fox jumps\n"
           "       the lazy   dog.       over\n    : foxes and   dogs!     words\n"
           "    quick brown   fox jumps over\n   other words:   foxes and dogs!\n"
           "      brown fox   jumps over the\n       over the   lazy dog.\n"
           "       A second   line, with\n     line, with   other words:\n"
           "      fox jumps   over the lazy\n   jumps    The   quick brown fox\n"
           "   with       A   second line,\n     jumps over   the lazy dog.\n"
           "   second line,   with other    A\n   , with other   words: foxes\n",
           0);
    // ptx -M zz -O -w 30 a.txt
    expect("-M zz -O -w 30 a.txt",
           "", ".zz \", with/\" \"\" \"A second line\" \"\"\n.zz \"brown/\" \"\" \"The quick\" \"\"\n"
           ".zz \"\" \"/: foxes\" \"and dogs!\" \"\"\n.zz \"\" \"The quick\" \"brown fox/\" \"\"\n"
           ".zz \"\" \"the lazy\" \"dog.\" \"/over\"\n.zz \"\" \"foxes and\" \"dogs!\" \"/:\"\n"
           ".zz \"\" \"/brown\" \"fox jumps/\" \"\"\n.zz \"\" \"/words:\" \"foxes and/\" \"\"\n"
           ".zz \"\" \"/brown fox\" \"jumps over/\" \"\"\n.zz \"\" \"/over the\" \"lazy dog.\" \"\"\n"
           ".zz \"\" \"A second\" \"line, with/\" \"\"\n.zz \"\" \"/line, with\" \"other words:/\" \"\"\n"
           ".zz \"\" \"/fox jumps\" \"over the lazy/\" \"\"\n.zz \"fox/\" \"The\" \"quick brown\" \"\"\n"
           ".zz \"with/\" \"A\" \"second line,\" \"\"\n.zz \"\" \"/jumps over\" \"the lazy dog.\" \"\"\n"
           ".zz \"\" \"/line,\" \"with other/\" \"\"\n.zz \"\" \"/with other\" \"words: foxes/\" \"\"\n",
           0);
    // ptx -w 10 a.txt
    expect("-w 10 a.txt",
           "", "        A/\n        The/\n     /   and/\n     /   brown/\n     /   dog/\n"
           "     /   dogs/\n     /   fox/\n     /   foxes/\n     /   jumps/\n"
           "     /   lazy/\n     /   line/\n     /   other/\n     /   over/\n"
           "     /   quick/\n     /   second/\n     /   the/\n     /   with/\n"
           "     /   words/\n",
           0);
    // ptx -w 60 b.txt
    expect("-w 60 b.txt",
           "", "          abc_def 123 x9y it's   a-b\n                                 abc_def 123 x9y it's a-b\n"
           "       abc_def 123 x9y it's a-   b\n                          abc_   def 123 x9y it's a-b\n"
           "               abc_def 123 x9y   it's a-b\n           abc_def 123 x9y it'   s a-b\n"
           "                   abc_def 123   x9y it's a-b\n                abc_def 123 x9   y it's a-b\n",
           0);
    // ptx -G -w 60 b.txt
    expect("-G -w 60 b.txt",
           "", ".xx \"\" \"abc_def\" \"123 x9y it's a-b\" \"\"\n.xx \"\" \"abc_def 123 x9y it's\" \"a-b\" \"\"\n"
           ".xx \"\" \"\" \"abc_def 123 x9y it's a-b\" \"\"\n.xx \"\" \"abc_def 123 x9y\" \"it's a-b\" \"\"\n"
           ".xx \"\" \"abc_def 123\" \"x9y it's a-b\" \"\"\n",
           0);
    // ptx -w 60 c.txt
    expect("-w 60 c.txt",
           "", "                          four   five?\n                                 four five?\n"
           "                                 one. two  three.\n                      one. two   three.\n"
           "                          one.   two  three.\n",
           0);
    // ptx -G -w 60 c.txt
    expect("-G -w 60 c.txt",
           "", ".xx \"\" \"\" \"five?\" \"\"\n.xx \"\" \"one. two  three.\" \"four\" \"\"\n"
           ".xx \"\" \"\" \"one. two  three.  four\" \"\"\n.xx \"\" \"one. two\" \"three.  four\" \"\"\n"
           ".xx \"\" \"one.\" \"two  three.  four\" \"\"\n",
           0);
    // ptx -w 60 d.txt
    expect("-w 60 d.txt",
           "", "       Cat the cat THE CAT a b   A B                        the\n"
           "     Cat the cat THE CAT a b A   B                          the\n"
           "           the Cat the cat THE   CAT a b A B\n                           the   Cat the cat THE CAT a b A B\n"
           "               the Cat the cat   THE CAT a b A B\n       the Cat the cat THE CAT   a b A B\n"
           "     the Cat the cat THE CAT a   b A B\n                   the Cat the   cat THE CAT a b A B\n"
           "   A B                           the Cat the cat THE CAT a b\n"
           "                       the Cat   the cat THE CAT a b A B\n",
           0);
    // ptx -f -w 60 d.txt
    expect("-f -w 60 d.txt",
           "", "       the Cat the cat THE CAT   a b A B\n       Cat the cat THE CAT a b   A B                        the\n"
           "     the Cat the cat THE CAT a   b A B\n     Cat the cat THE CAT a b A   B                          the\n"
           "                           the   Cat the cat THE CAT a b A B\n"
           "                   the Cat the   cat THE CAT a b A B\n           the Cat the cat THE   CAT a b A B\n"
           "   A B                           the Cat the cat THE CAT a b\n"
           "                       the Cat   the cat THE CAT a b A B\n"
           "               the Cat the cat   THE CAT a b A B\n",
           0);
    // ptx -i ign.txt -w 40 d.txt
    expect("-i ign.txt -w 40 d.txt",
           "", "     cat THE CAT a b   A B             /the\n       THE CAT a b A   B           /the cat\n"
           "     Cat the cat THE   CAT a b A B      the\n   CAT a b/      the   Cat the cat THE\n"
           "     the Cat the cat   THE CAT a b A B\n       cat THE CAT a   b A B       /Cat the\n"
           "         the Cat the   cat THE CAT a b A/\n",
           0);
    // ptx -o only.txt -w 40 d.txt
    expect("-o only.txt -w 40 d.txt",
           "", "       THE CAT a b A   B           /the cat\n         the Cat the   cat THE CAT a b A/\n",
           0);
    // ptx -f -o only.txt -w 40 d.txt
    expect("-f -o only.txt -w 40 d.txt",
           "", "       cat THE CAT a   b A B       /Cat the\n       THE CAT a b A   B           /the cat\n"
           "   CAT a b/      the   Cat the cat THE\n         the Cat the   cat THE CAT a b A/\n"
           "     Cat the cat THE   CAT a b A B      the\n",
           0);
    // ptx -r r.txt
    expect("-r r.txt",
           "", "ref22                                   a dog ran\nref1                              the   cat sat\n"
           "ref22                               a   dog ran\nref22                           a dog   ran\n"
           "ref1                          the cat   sat\nref1                                    the cat sat\n",
           0);
    // ptx -r -O r.txt
    expect("-r -O r.txt",
           "", ".xx \"\" \"\" \"a dog ran\" \"\" \"ref22\"\n.xx \"\" \"the\" \"cat sat\" \"\" \"ref1\"\n"
           ".xx \"\" \"a\" \"dog ran\" \"\" \"ref22\"\n.xx \"\" \"a dog\" \"ran\" \"\" \"ref22\"\n"
           ".xx \"\" \"the cat\" \"sat\" \"\" \"ref1\"\n.xx \"\" \"\" \"the cat sat\" \"\" \"ref1\"\n",
           0);
    // ptx -r -R r.txt
    expect("-r -R r.txt",
           "", "                                    a dog ran                              ref22\n"
           "                              the   cat sat                                ref1\n"
           "                                a   dog ran                                ref22\n"
           "                            a dog   ran                                    ref22\n"
           "                          the cat   sat                                    ref1\n"
           "                                    the cat sat                            ref1\n",
           0);
    // ptx -O s.txt
    expect("-O s.txt",
           "", ".xx \"\" \"say \"\"hi\"\" {x} $5 & 100% #1 _u\" \"a\\b\" \"\"\n.xx \"\" \"say \"\"hi\"\" {x} $5 & 100% #1 _u a\\\" \"b\" \"\"\n"
           ".xx \"\" \"say \"\"\" \"hi\"\" {x} $5 & 100% #1 _u a\\b\" \"\"\n.xx \"\" \"\" \"say \"\"hi\"\" {x} $5 & 100% #1 _u a\\b\" \"\"\n"
           ".xx \"\" \"say \"\"hi\"\" {x} $5 & 100% #1 _\" \"u a\\b\" \"\"\n.xx \"\" \"say \"\"hi\"\" {\" \"x} $5 & 100% #1 _u a\\b\" \"\"\n",
           0);
    // ptx -T s.txt
    expect("-T s.txt",
           "", "\\xx {}{say \"hi\" $\\{$x$\\}$ \\$5 \\& 100\\% \\#1 \\_u}{a}{\\backslash{}b}{}\n"
           "\\xx {}{say \"hi\" $\\{$x$\\}$ \\$5 \\& 100\\% \\#1 \\_u a\\backslash{}}{b}{}{}\n"
           "\\xx {}{say \"}{hi}{\" $\\{$x$\\}$ \\$5 \\& 100\\% \\#1 \\_u a\\backslash{}b}{}\n"
           "\\xx {}{}{say}{ \"hi\" $\\{$x$\\}$ \\$5 \\& 100\\% \\#1 \\_u a\\backslash{}b}{}\n"
           "\\xx {}{say \"hi\" $\\{$x$\\}$ \\$5 \\& 100\\% \\#1 \\_}{u}{ a\\backslash{}b}{}\n"
           "\\xx {}{say \"hi\" $\\{$}{x}{$\\}$ \\$5 \\& 100\\% \\#1 \\_u a\\backslash{}b}{}\n",
           0);
    // ptx -w 40 t.txt
    expect("-w 40 t.txt",
           "", "                       a b  c\n                   a   b  c\n"
           "                 a b   c\n",
           0);
    // ptx -b brk.txt -w 50 b.txt
    expect("-b brk.txt -w 50 b.txt",
           "", "   b                        abc_def 123 x9y it's a-\n     _def 123 x9y it's a-   b                     abc\n"
           "                     abc_   def 123 x9y it's a-b\n",
           0);
    // ptx -W "[a-z]+" -w 50 b.txt
    expect("-W \"[a-z]+\" -w 50 b.txt",
           "", "     abc_def 123 x9y it's   a-b\n   b                        abc_def 123 x9y it's a-\n"
           "     _def 123 x9y it's a-   b                     abc\n                     abc_   def 123 x9y it's a-b\n"
           "          abc_def 123 x9y   it's a-b\n      abc_def 123 x9y it'   s a-b\n"
           "              abc_def 123   x9y it's a-b\n           abc_def 123 x9   y it's a-b\n",
           0);
    // ptx -S ";" -w 50 c.txt
    expect("-S \";\" -w 50 c.txt",
           "", "      . two  three.  four   five?                 one\n         one. two  three.   four five?\n"
           "   five?                    one. two  three.  four\n                 one. two   three.  four five?\n"
           "                     one.   two  three.  four five?\n",
           0);
    // ptx -w 30 l.txt
    expect("-w 30 l.txt",
           "", "      /is long.   Short ones: a/\n   is/            Supercalifragilisticexpialidocious\n"
           "         /ones:   a b c.\n        ones: a   b c.     /Short\n"
           "      ones: a b   c.     /. Short\n   Short/      /   is long.\n"
           "   ones/    /is   long. Short\n       /. Short   ones: a b c.\n",
           0);
    // ptx -A a.txt c.txt
    expect("-A a.txt c.txt",
           "", "a.txt:2:  words: foxes and dogs!         A second line, with other\n"
           "a.txt:1:  over the lazy dog.             The quick brown fox jumps\n"
           "a.txt:2:     , with other words: foxes   and dogs!                 /line\n"
           "a.txt:1:  dog.               The quick   brown fox jumps over the lazy\n"
           "a.txt:1:       fox jumps over the lazy   dog.               /quick brown\n"
           "a.txt:2:        other words: foxes and   dogs!               /line, with\n"
           "c.txt:2:                          four   five?\nc.txt:1:                                 four five?\n"
           "a.txt:1:               The quick brown   fox jumps over the lazy dog.\n"
           "a.txt:2:       line, with other words:   foxes and dogs!        A second\n"
           "a.txt:1:           The quick brown fox   jumps over the lazy dog.\n"
           "a.txt:1:      brown fox jumps over the   lazy dog.             The quick\n"
           "a.txt:2:  and dogs!           A second   line, with other words: foxes\n"
           "c.txt:1:                                 one. two  three.\n"
           "a.txt:2:           A second line, with   other words: foxes and dogs!\n"
           "a.txt:1:     The quick brown fox jumps   over the lazy dog.\n"
           "a.txt:1:  the lazy dog.            The   quick brown fox jumps over\n"
           "a.txt:2:  : foxes and dogs!          A   second line, with other words\n"
           "a.txt:1:    quick brown fox jumps over   the lazy dog.               The\n"
           "c.txt:1:                      one. two   three.\nc.txt:1:                          one.   two  three.\n"
           "a.txt:2:  dogs!         A second line,   with other words: foxes and\n"
           "a.txt:2:     A second line, with other   words: foxes and dogs!\n",
           0);

    printf("standard input, -G with an output file\n");
    expect("-w 30", "alpha beta\n", "                  alpha beta\n          alpha   beta\n", 0);
    expect("-A -w 30", "alpha beta\n", ":1:              alpha beta\n:1:      alpha   beta\n", 0);
    expect("-t -w 20", "x y\n", "             x y\n         x   y\n", 0);
    expect("-w 30 -", "alpha beta\n", "                  alpha beta\n          alpha   beta\n", 0);
    expect("", "", "", 0);
    expect("", "123 456\n", "", 0);                                          // no word at all
    expect("-G -w 30 a.txt out.txt", "", "", 0);
    expect_content(workDir, "out.txt", ".xx \"line,/\" \"\" \"A second\" \"\"\n"
                                       ".xx \"brown/\" \"\" \"The quick\" \"\"\n"
                                       ".xx \"\" \"/words: foxes\" \"and dogs!\" \"\"\n"
                                       ".xx \"\" \"The quick\" \"brown fox/\" \"\"\n"
                                       ".xx \"\" \"the lazy\" \"dog.\" \"/over\"\n"
                                       ".xx \"\" \"/foxes and\" \"dogs!\" \"\"\n"
                                       ".xx \"\" \"/quick brown\" \"fox jumps/\" \"\"\n"
                                       ".xx \"\" \"/other words:\" \"foxes and/\" \"\"\n"
                                       ".xx \"\" \"/brown fox\" \"jumps over/\" \"\"\n"
                                       ".xx \"\" \"/over the\" \"lazy dog.\" \"\"\n"
                                       ".xx \"\" \"A second\" \"line, with/\" \"\"\n"
                                       ".xx \"\" \"/line, with\" \"other words:/\" \"\"\n"
                                       ".xx \"\" \"/fox jumps\" \"over the/\" \"\"\n"
                                       ".xx \"fox/\" \"The\" \"quick brown\" \"\"\n"
                                       ".xx \"with/\" \"A\" \"second line,\" \"\"\n"
                                       ".xx \"\" \"/jumps over\" \"the lazy/\" \"\"\n"
                                       ".xx \"\" \"/second line,\" \"with other/\" \"\"\n"
                                       ".xx \"\" \"/with other\" \"words: foxes/\" \"\"\n");

    printf("errors\n");
    message("nosuchfile_zz", "", "ptx: nosuchfile_zz: No such file or directory", 1);
    message("-i nosuchfile_zz a.txt", "", "ptx: nosuchfile_zz: No such file or directory", 1);
    message("-g 0 a.txt", "", "invalid gap width: '0'", 1);
    message("-g x a.txt", "", "invalid gap width: 'x'", 1);
    message("-w 0 a.txt", "", "invalid line width: '0'", 1);
    message("-W \"\\(\" a.txt", "", "Unmatched ( or \\(", 1);
    message("-S \"x*\" a.txt", "", "regular expression has a match of length zero: 'x*'", 1);
    message("--format=bogus a.txt", "", "invalid argument 'bogus' for '--format'", 1);
    message("-G a.txt out.txt extra", "", "extra operand 'extra'", 1);
    message("--bogus", "", "unrecognized option", 1);
    expect_run("ptx", "--version", "", "ptx (Winshell) 0.1\n", 0);
    return test_summary("ptx_test");
}
