/*
 * Tests of the shell with text that is not ASCII: names of files and directories, wildcards that
 * match a character and not a byte, variables, quotes and redirection. Everything is UTF-8, which the
 * programs get from their manifest (common/utf8.manifest); that needs Windows 10 version 1903 or later,
 * and the test says so when it runs on an older one.
 */

#include "testtools.h"

static void write_in(const char* directory, const char* name, const char* text) {
    char path[MAX_PATH * 3];
    FILE* f;
    snprintf(path, sizeof path, "%s\\%s", directory, name);
    f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static int has_file(const char* directory, const char* name) {
    char path[MAX_PATH * 3];
    snprintf(path, sizeof path, "%s\\%s", directory, name);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void test_words(void) {
    printf("words and variables\n");

    SCRIPT_OUT("echo 한글 가나다\n", "한글 가나다\n");
    SCRIPT_OUT("echo \"가 나\" '다  라'\n", "가 나 다  라\n");
    SCRIPT_OUT("export WORD=한글; echo $WORD ${WORD}\n", "한글 한글\n");
    SCRIPT_OUT("W=가나; echo \"$W\"x\n", "가나x\n");

    // the programs get the variable in UTF-8 as well
    SCRIPT_OUT("export KOREAN=값; printenv KOREAN\n", "값\n");
    SCRIPT_OUT("KOREAN=값 printenv KOREAN\n", "값\n");

    // text goes through a pipe and a tool unchanged, and a character is counted as one
    SCRIPT_OUT("echo 가나다 | grep 나\n", "가나다\n");
    SCRIPT_OUT("echo 가나다 | cut -c2\n", "나\n");
    SCRIPT_OUT("echo -n 가나다 | wc -m\n", "3\n");
}

static void test_names(void) {
    char directory[MAX_PATH * 3];
    char expected[MAX_PATH * 4];

    printf("names of files and directories\n");

    snprintf(directory, sizeof directory, "%s\\한글 폴더", workDir);
    reset_dir(directory);                   // the files of an earlier run are not wanted
    write_in(directory, "가.txt", "가\n");
    write_in(directory, "나.txt", "나\n");
    write_in(directory, "ab.txt", "ab\n");
    write_in(directory, "É.txt", "E\n");
    write_in(directory, "e.log", "e\n");

    // the files as typed, and the directory the shell stands in
    SCRIPT_OUT("cat \"한글 폴더/가.txt\"\n", "가\n");
    snprintf(expected, sizeof expected, "%s/한글 폴더\n", virtualWorkDir);
    SCRIPT_OUT("cd \"한글 폴더\"\npwd\n", expected);
    SCRIPT_OUT("cd \"한글 폴더\"; echo $PWD\n", expected);

    // wildcards: ? is one character, whatever it takes in bytes; names come in the order of their bytes
    SCRIPT_OUT("cd \"한글 폴더\"; echo ?.txt\n", "É.txt 가.txt 나.txt\n");
    SCRIPT_OUT("cd \"한글 폴더\"; echo *.txt\n", "ab.txt É.txt 가.txt 나.txt\n");
    SCRIPT_OUT("cd \"한글 폴더\"; echo ??.txt\n", "ab.txt\n");
    SCRIPT_OUT("cd \"한글 폴더\"; echo [가-나].txt\n", "가.txt 나.txt\n");
    SCRIPT_OUT("cd \"한글 폴더\"; echo [!가-나]*.txt\n", "ab.txt É.txt\n");
    SCRIPT_OUT("cd \"한글 폴더\"; echo [가].txt\n", "가.txt\n");
    SCRIPT_OUT("cd \"한글 폴더\"; echo [é]*\n", "É.txt\n");                 // case is ignored for Latin letters too
    SCRIPT_OUT("cd \"한글 폴더\"; echo 가*\n", "가.txt\n");
    SCRIPT_OUT("cd \"한글 폴더\"; echo *.zzz\n", "*.zzz\n");                  // no match: as typed
    SCRIPT_OUT("cd \"한글 폴더\"; cat ?.txt | wc -l\n", "3\n");

    // a quoted wildcard is not one
    SCRIPT_OUT("cd \"한글 폴더\"; echo '?.txt' \"[가].txt\"\n", "?.txt [가].txt\n");

    // redirection makes names of that kind, and the tools find them again
    SCRIPT_OUT("echo 내용 > \"한글 폴더/새 파일.txt\"; cat \"한글 폴더/새 파일.txt\"\n", "내용\n");
    CHECK(has_file(directory, "새 파일.txt"));

    SCRIPT_OUT("cd \"한글 폴더\"; cp 가.txt 다.txt; mv 나.txt 라.txt\n", "");
    CHECK(has_file(directory, "다.txt"));
    CHECK(has_file(directory, "라.txt"));
    CHECK(!has_file(directory, "나.txt"));

    SCRIPT_OUT("cd \"한글 폴더\"; rm 다.txt 라.txt; echo *.txt\n", "ab.txt É.txt 가.txt 새 파일.txt\n");
    CHECK(!has_file(directory, "다.txt"));

    // a directory with such a name: made, entered, left
    SCRIPT_OUT("mkdir 새폴더; cd 새폴더; cd ..; echo 새*\n", "새폴더\n");
}

int main(void) {
    tools_init("shell_utf8_test");

    if (GetACP() != CP_UTF8) {
        printf("skipped: this Windows does not run programs in the UTF-8 code page (needs Windows 10 1903)\n");
        return test_summary("shell_utf8_test");
    }

    test_words();
    test_names();
    return test_summary("shell_utf8_test");
}
