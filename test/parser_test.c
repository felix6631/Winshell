/*
 * Unit tests of the command line parser of the shell (shell/src/parser.c):
 * words, quoting, pipes and redirections.
 */

#include "testlib.h"
#include "parser.h"

/** Parses line and checks the number of commands; returns 0 when the parse worked. */
static int parse(const char* line, Pipeline* pipeline) {
    return winshell_parse(line, pipeline);
}

static int argument_count(const Command* command) {
    int count = 0;
    while (command->argv[count] != NULL)
        count++;
    return count;
}

/** Silences the messages the parser prints for bad input. */
static FILE* mute_stderr(void) {
    fflush(stderr);
    return freopen("NUL", "w", stderr);
}

static void test_words(void) {
    Pipeline p;

    printf("words\n");
    CHECK_INT(parse("echo hello world", &p), 0);
    CHECK_INT(p.count, 1);
    CHECK_INT(argument_count(&p.commands[0]), 3);
    CHECK_STR(p.commands[0].argv[0], "echo");
    CHECK_STR(p.commands[0].argv[1], "hello");
    CHECK_STR(p.commands[0].argv[2], "world");
    CHECK(p.commands[0].argv[3] == NULL);
    CHECK(p.commands[0].input == NULL && p.commands[0].output == NULL && p.commands[0].errput == NULL);
    winshell_freePipeline(&p);

    // blanks of any kind separate words; leading and trailing ones are ignored
    CHECK_INT(parse("  \techo \t a   b \r\n", &p), 0);
    CHECK_INT(argument_count(&p.commands[0]), 3);
    CHECK_STR(p.commands[0].argv[0], "echo");
    CHECK_STR(p.commands[0].argv[1], "a");
    CHECK_STR(p.commands[0].argv[2], "b");
    winshell_freePipeline(&p);

    // an empty line is a pipeline of nothing
    CHECK_INT(parse("", &p), 0);
    CHECK_INT(p.count, 0);
    winshell_freePipeline(&p);
    CHECK_INT(parse("   \t ", &p), 0);
    CHECK_INT(p.count, 0);
    winshell_freePipeline(&p);

    // Windows paths are plain words
    CHECK_INT(parse("cat C:\\Users\\me\\file.txt /c/x", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "C:\\Users\\me\\file.txt");
    CHECK_STR(p.commands[0].argv[2], "/c/x");
    winshell_freePipeline(&p);
}

static void test_many_arguments(void) {
    Pipeline p;
    char* line = malloc(64 * 1024);
    size_t length = 0;
    int i;

    printf("many arguments\n");
    length += (size_t)sprintf(line, "echo");
    for (i = 1; i <= 5000; i++)
        length += (size_t)sprintf(line + length, " arg%d", i);

    CHECK_INT(parse(line, &p), 0);
    CHECK_INT(p.count, 1);
    CHECK_INT(argument_count(&p.commands[0]), 5001);
    CHECK_STR(p.commands[0].argv[1], "arg1");
    CHECK_STR(p.commands[0].argv[5000], "arg5000");
    winshell_freePipeline(&p);
    free(line);
}

static void test_quotes(void) {
    Pipeline p;

    printf("quotes\n");

    CHECK_INT(parse("echo \"a b\" c", &p), 0);
    CHECK_INT(argument_count(&p.commands[0]), 3);
    CHECK_STR(p.commands[0].argv[1], "a b");
    CHECK_STR(p.commands[0].argv[2], "c");
    winshell_freePipeline(&p);

    CHECK_INT(parse("echo 'a b' c", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "a b");
    winshell_freePipeline(&p);

    // quoted and plain parts make one word
    CHECK_INT(parse("echo a\"b c\"d", &p), 0);
    CHECK_INT(argument_count(&p.commands[0]), 2);
    CHECK_STR(p.commands[0].argv[1], "ab cd");
    winshell_freePipeline(&p);

    // empty quotes are an empty argument
    CHECK_INT(parse("echo \"\" x ''", &p), 0);
    CHECK_INT(argument_count(&p.commands[0]), 4);
    CHECK_STR(p.commands[0].argv[1], "");
    CHECK_STR(p.commands[0].argv[2], "x");
    CHECK_STR(p.commands[0].argv[3], "");
    winshell_freePipeline(&p);

    // \" is a quote inside double quotes; single quotes keep everything
    CHECK_INT(parse("echo \"say \\\"hi\\\"\"", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "say \"hi\"");
    winshell_freePipeline(&p);

    CHECK_INT(parse("echo 'a\\b\"c'", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "a\\b\"c");
    winshell_freePipeline(&p);

    // a quote of the other kind is a plain character
    CHECK_INT(parse("echo \"it's\" 'say \"x\"'", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "it's");
    CHECK_STR(p.commands[0].argv[2], "say \"x\"");
    winshell_freePipeline(&p);

    // backslashes are only special in front of a double quote
    CHECK_INT(parse("echo C:\\bin\\echo \"C:\\Program Files\\x\" \"dir\\\\\"", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "C:\\bin\\echo");
    CHECK_STR(p.commands[0].argv[2], "C:\\Program Files\\x");
    CHECK_STR(p.commands[0].argv[3], "dir\\");
    winshell_freePipeline(&p);

    // operators inside quotes are text
    CHECK_INT(parse("echo \"a>b\" 'c|d' \"<\"", &p), 0);
    CHECK_INT(p.count, 1);
    CHECK_STR(p.commands[0].argv[1], "a>b");
    CHECK_STR(p.commands[0].argv[2], "c|d");
    CHECK_STR(p.commands[0].argv[3], "<");
    CHECK(p.commands[0].output == NULL);
    winshell_freePipeline(&p);

    // unclosed quotes are errors
    mute_stderr();
    CHECK_INT(parse("echo \"abc", &p), -1);
    CHECK_INT(parse("echo 'abc", &p), -1);
    CHECK_INT(parse("echo \"a\\\"", &p), -1);
}

static void test_pipes(void) {
    Pipeline p;

    printf("pipes\n");
    CHECK_INT(parse("echo hi | cat -n | cat", &p), 0);
    CHECK_INT(p.count, 3);
    CHECK_STR(p.commands[0].argv[0], "echo");
    CHECK_STR(p.commands[1].argv[0], "cat");
    CHECK_STR(p.commands[1].argv[1], "-n");
    CHECK_STR(p.commands[2].argv[0], "cat");
    CHECK_INT(argument_count(&p.commands[2]), 1);
    winshell_freePipeline(&p);

    // no spaces needed
    CHECK_INT(parse("echo hi|cat", &p), 0);
    CHECK_INT(p.count, 2);
    CHECK_STR(p.commands[0].argv[1], "hi");
    winshell_freePipeline(&p);

    mute_stderr();
    CHECK_INT(parse("echo hi |", &p), -1);
    CHECK_INT(parse("| cat", &p), -1);
    CHECK_INT(parse("echo hi | | cat", &p), -1);
}

static void test_redirections(void) {
    Pipeline p;

    printf("redirections\n");
    CHECK_INT(parse("cat < in.txt > out.txt", &p), 0);
    CHECK_INT(p.count, 1);
    CHECK_INT(argument_count(&p.commands[0]), 1);
    CHECK_STR(p.commands[0].input, "in.txt");
    CHECK_STR(p.commands[0].output, "out.txt");
    CHECK_INT(p.commands[0].append, 0);
    winshell_freePipeline(&p);

    CHECK_INT(parse("echo hi >> log.txt", &p), 0);
    CHECK_STR(p.commands[0].output, "log.txt");
    CHECK_INT(p.commands[0].append, 1);
    CHECK_INT(argument_count(&p.commands[0]), 2);
    winshell_freePipeline(&p);

    CHECK_INT(parse("cat nosuch 2> err.txt", &p), 0);
    CHECK_STR(p.commands[0].errput, "err.txt");
    CHECK_INT(p.commands[0].errAppend, 0);
    CHECK_INT(argument_count(&p.commands[0]), 2);
    winshell_freePipeline(&p);

    CHECK_INT(parse("cat nosuch 2>> err.txt", &p), 0);
    CHECK_STR(p.commands[0].errput, "err.txt");
    CHECK_INT(p.commands[0].errAppend, 1);
    winshell_freePipeline(&p);

    // no spaces needed, and the redirection may come first
    CHECK_INT(parse("echo hi>out.txt", &p), 0);
    CHECK_INT(argument_count(&p.commands[0]), 2);
    CHECK_STR(p.commands[0].argv[1], "hi");
    CHECK_STR(p.commands[0].output, "out.txt");
    winshell_freePipeline(&p);

    CHECK_INT(parse("> out.txt echo hi", &p), 0);
    CHECK_STR(p.commands[0].argv[0], "echo");
    CHECK_STR(p.commands[0].output, "out.txt");
    winshell_freePipeline(&p);

    // a 2 is a file descriptor only when it sticks to the operator
    CHECK_INT(parse("echo 2 > out.txt", &p), 0);
    CHECK_INT(argument_count(&p.commands[0]), 2);
    CHECK_STR(p.commands[0].argv[1], "2");
    CHECK_STR(p.commands[0].output, "out.txt");
    CHECK(p.commands[0].errput == NULL);
    winshell_freePipeline(&p);

    CHECK_INT(parse("echo a2>out.txt", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "a2");
    CHECK_STR(p.commands[0].output, "out.txt");
    winshell_freePipeline(&p);

    // the last of two redirections of the same kind wins
    CHECK_INT(parse("echo hi > a.txt > b.txt", &p), 0);
    CHECK_STR(p.commands[0].output, "b.txt");
    winshell_freePipeline(&p);

    // a pipeline with redirections on its ends
    CHECK_INT(parse("cat < in.txt | cat -n > out.txt", &p), 0);
    CHECK_INT(p.count, 2);
    CHECK_STR(p.commands[0].input, "in.txt");
    CHECK(p.commands[0].output == NULL);
    CHECK(p.commands[1].input == NULL);
    CHECK_STR(p.commands[1].output, "out.txt");
    winshell_freePipeline(&p);

    // quoted file names
    CHECK_INT(parse("echo hi > \"my file.txt\"", &p), 0);
    CHECK_STR(p.commands[0].output, "my file.txt");
    winshell_freePipeline(&p);

    mute_stderr();
    CHECK_INT(parse("echo hi >", &p), -1);
    CHECK_INT(parse("echo hi > | cat", &p), -1);
    CHECK_INT(parse("cat <", &p), -1);
    CHECK_INT(parse("> out.txt", &p), -1);
    CHECK_INT(parse("echo hi | > out.txt", &p), -1);
}

static void test_patterns(void) {
    Pipeline p;

    printf("glob patterns\n");

    // an unquoted wildcard makes the word a pattern; the word itself is kept
    CHECK_INT(parse("ls *.c file", &p), 0);
    CHECK(p.commands[0].patterns != NULL);
    CHECK(p.commands[0].patterns[0] == NULL);
    CHECK_STR(p.commands[0].argv[1], "*.c");
    CHECK_STR(p.commands[0].patterns[1], "*.c");
    CHECK(p.commands[0].patterns[2] == NULL);
    CHECK(p.commands[0].patterns[3] == NULL);
    winshell_freePipeline(&p);

    CHECK_INT(parse("ls a?b [a-c]x [!a]", &p), 0);
    CHECK_STR(p.commands[0].patterns[1], "a?b");
    CHECK_STR(p.commands[0].patterns[2], "[a-c]x");
    CHECK_STR(p.commands[0].patterns[3], "[!a]");
    winshell_freePipeline(&p);

    // quoted wildcards are not patterns
    CHECK_INT(parse("ls \"*.c\" '?' a\"*\"", &p), 0);
    CHECK(p.commands[0].patterns[1] == NULL);
    CHECK(p.commands[0].patterns[2] == NULL);
    CHECK(p.commands[0].patterns[3] == NULL);
    CHECK_STR(p.commands[0].argv[3], "a*");
    winshell_freePipeline(&p);

    // ... but they stay escaped inside a word that has an unquoted one
    CHECK_INT(parse("ls \"a*\"*", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "a**");
    CHECK(p.commands[0].patterns[1] != NULL);
    CHECK_INT(p.commands[0].patterns[1][0], 'a');
    CHECK_INT(p.commands[0].patterns[1][1], GLOB_ESCAPE);
    CHECK_INT(p.commands[0].patterns[1][2], '*');
    CHECK_INT(p.commands[0].patterns[1][3], '*');
    CHECK_INT(p.commands[0].patterns[1][4], 0);
    winshell_freePipeline(&p);

    // a [ without ] is an ordinary character
    CHECK_INT(parse("ls a[b [ x]", &p), 0);
    CHECK(p.commands[0].patterns[1] == NULL);
    CHECK(p.commands[0].patterns[2] == NULL);
    CHECK(p.commands[0].patterns[3] == NULL);
    winshell_freePipeline(&p);

    // file names of redirections are never patterns
    CHECK_INT(parse("echo hi > *.out", &p), 0);
    CHECK_STR(p.commands[0].output, "*.out");
    winshell_freePipeline(&p);

    // many words with patterns
    CHECK_INT(parse("a *1 *2 *3 *4 *5 *6 *7 *8 *9 *10", &p), 0);
    CHECK_STR(p.commands[0].patterns[10], "*10");
    CHECK(p.commands[0].patterns[11] == NULL);
    winshell_freePipeline(&p);
}

static void test_variables(void) {
    Pipeline p;

    printf("variables\n");
    _putenv_s("PT_VAR", "hello world");
    _putenv_s("PT_SPACES", "a   b");
    _putenv_s("PT_GLOB", "*.c");
    _putenv_s("PT_OP", "a|b>c");
    _putenv_s("PT_NAME", "name");

    // outside of quotes: cut at blanks into words
    CHECK_INT(parse("echo $PT_VAR", &p), 0);
    CHECK_INT(argument_count(&p.commands[0]), 3);
    CHECK_STR(p.commands[0].argv[1], "hello");
    CHECK_STR(p.commands[0].argv[2], "world");
    winshell_freePipeline(&p);

    // in double quotes: one word
    CHECK_INT(parse("echo \"$PT_VAR\" x", &p), 0);
    CHECK_INT(argument_count(&p.commands[0]), 3);
    CHECK_STR(p.commands[0].argv[1], "hello world");
    CHECK_STR(p.commands[0].argv[2], "x");
    winshell_freePipeline(&p);

    CHECK_INT(parse("echo \"$PT_SPACES\"", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "a   b");
    winshell_freePipeline(&p);

    // in single quotes: as it is
    CHECK_INT(parse("echo '$PT_VAR'", &p), 0);
    CHECK_INT(argument_count(&p.commands[0]), 2);
    CHECK_STR(p.commands[0].argv[1], "$PT_VAR");
    winshell_freePipeline(&p);

    // braces, and text around the variable
    CHECK_INT(parse("echo ${PT_NAME}x pre$PT_NAME-post", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "namex");
    CHECK_STR(p.commands[0].argv[2], "prename-post");
    winshell_freePipeline(&p);

    CHECK_INT(parse("echo \"[$PT_NAME]\"", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "[name]");
    winshell_freePipeline(&p);

    // a variable that is not set is nothing; in quotes it is an empty word
    CHECK_INT(parse("echo $PT_NOT_SET x", &p), 0);
    CHECK_INT(argument_count(&p.commands[0]), 2);
    CHECK_STR(p.commands[0].argv[1], "x");
    winshell_freePipeline(&p);

    CHECK_INT(parse("echo \"$PT_NOT_SET\" x", &p), 0);
    CHECK_INT(argument_count(&p.commands[0]), 3);
    CHECK_STR(p.commands[0].argv[1], "");
    winshell_freePipeline(&p);

    CHECK_INT(parse("echo [$PT_NOT_SET]", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "[]");
    winshell_freePipeline(&p);

    // a $ that starts no name is a $
    CHECK_INT(parse("echo $ a$ $1 \"$\" ${ ${} $-", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "$");
    CHECK_STR(p.commands[0].argv[2], "a$");
    CHECK_STR(p.commands[0].argv[3], "$1");
    CHECK_STR(p.commands[0].argv[4], "$");
    CHECK_STR(p.commands[0].argv[5], "${");
    CHECK_STR(p.commands[0].argv[6], "${}");
    CHECK_STR(p.commands[0].argv[7], "$-");
    winshell_freePipeline(&p);

    // the status of the last command
    winshell_setLastStatus(7);
    CHECK_INT(parse("echo $?", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "7");
    winshell_freePipeline(&p);
    winshell_setLastStatus(0);

    // the value is text: operators and quotes in it are nothing
    CHECK_INT(parse("echo $PT_OP", &p), 0);
    CHECK_INT(p.count, 1);
    CHECK_INT(argument_count(&p.commands[0]), 2);
    CHECK_STR(p.commands[0].argv[1], "a|b>c");
    CHECK(p.commands[0].output == NULL);
    winshell_freePipeline(&p);

    // wildcards in a value act, unless it is quoted
    CHECK_INT(parse("ls $PT_GLOB \"$PT_GLOB\"", &p), 0);
    CHECK(p.commands[0].patterns[1] != NULL);
    CHECK(p.commands[0].patterns[2] == NULL);
    CHECK_STR(p.commands[0].argv[2], "*.c");
    winshell_freePipeline(&p);

    // the home directory
    _putenv_s("HOME", "/c/home/me");
    CHECK_INT(parse("cd ~ ~/docs ~x a~ \"~\"", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "/c/home/me");
    CHECK_STR(p.commands[0].argv[2], "/c/home/me/docs");
    CHECK_STR(p.commands[0].argv[3], "~x");
    CHECK_STR(p.commands[0].argv[4], "a~");
    CHECK_STR(p.commands[0].argv[5], "~");
    winshell_freePipeline(&p);
    CHECK_INT(parse("echo $HOME", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "/c/home/me");
    winshell_freePipeline(&p);

    // in a redirection
    CHECK_INT(parse("echo hi > $PT_NAME.txt", &p), 0);
    CHECK_STR(p.commands[0].output, "name.txt");
    winshell_freePipeline(&p);

    // in a quoted word with a quote in front of it
    CHECK_INT(parse("echo \"say $PT_NAME\"", &p), 0);
    CHECK_STR(p.commands[0].argv[1], "say name");
    winshell_freePipeline(&p);
}

/** Cuts a line into its list and checks the number of items and their operators. */
static void test_lists(void) {
    CommandList list;

    printf("lists\n");

    CHECK_INT(winshell_parse_list("echo a; echo b", &list), 0);
    CHECK_INT(list.count, 2);
    CHECK_INT(list.items[0].op, LIST_FIRST);
    CHECK_INT(list.items[1].op, LIST_SEQ);
    CHECK_STR(list.items[0].text, "echo a");
    CHECK_STR(list.items[1].text, " echo b");
    winshell_freeList(&list);

    CHECK_INT(winshell_parse_list("a && b || c ; d", &list), 0);
    CHECK_INT(list.count, 4);
    CHECK_INT(list.items[0].op, LIST_FIRST);
    CHECK_INT(list.items[1].op, LIST_AND);
    CHECK_INT(list.items[2].op, LIST_OR);
    CHECK_INT(list.items[3].op, LIST_SEQ);
    winshell_freeList(&list);

    // no blanks needed
    CHECK_INT(winshell_parse_list("a;b&&c||d", &list), 0);
    CHECK_INT(list.count, 4);
    CHECK_STR(list.items[3].text, "d");
    winshell_freeList(&list);

    // a pipe is part of its command
    CHECK_INT(winshell_parse_list("a | b && c | d", &list), 0);
    CHECK_INT(list.count, 2);
    CHECK_STR(list.items[0].text, "a | b ");
    winshell_freeList(&list);

    // quotes keep the operators
    CHECK_INT(winshell_parse_list("echo \"a;b\" 'c&&d' \"e||f\"", &list), 0);
    CHECK_INT(list.count, 1);
    winshell_freeList(&list);

    CHECK_INT(winshell_parse_list("echo \"a\\\";b\" ; x", &list), 0);       // an escaped quote does not end it
    CHECK_INT(list.count, 2);
    winshell_freeList(&list);

    CHECK_INT(winshell_parse_list("echo 'it\"s;' ; x", &list), 0);
    CHECK_INT(list.count, 2);
    winshell_freeList(&list);

    // empty lines, a ; at the end
    CHECK_INT(winshell_parse_list("", &list), 0);
    CHECK_INT(list.count, 0);
    winshell_freeList(&list);
    CHECK_INT(winshell_parse_list("   ", &list), 0);
    CHECK_INT(list.count, 0);
    winshell_freeList(&list);
    CHECK_INT(winshell_parse_list("echo a ;", &list), 0);
    CHECK_INT(list.count, 1);
    winshell_freeList(&list);
    CHECK_INT(winshell_parse_list("echo a ; ", &list), 0);
    CHECK_INT(list.count, 1);
    winshell_freeList(&list);

    // errors
    mute_stderr();
    CHECK_INT(winshell_parse_list(";", &list), -1);
    CHECK_INT(winshell_parse_list("a ;; b", &list), -1);
    CHECK_INT(winshell_parse_list("&& a", &list), -1);
    CHECK_INT(winshell_parse_list("a &&", &list), -1);
    CHECK_INT(winshell_parse_list("a ||", &list), -1);
    CHECK_INT(winshell_parse_list("a && && b", &list), -1);
    CHECK_INT(winshell_parse_list("a & b", &list), -1);
    CHECK_INT(winshell_parse_list("a 2>&1", &list), -1);
}

int main(void) {
    test_variables();
    test_lists();
    test_patterns();
    test_words();
    test_many_arguments();
    test_quotes();
    test_pipes();
    test_redirections();
    return test_summary("parser_test");
}
