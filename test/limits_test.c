/*
 * Large inputs and boundaries: what happens when the input is much bigger than any buffer, when a
 * line is as long as the file, when a size is just on either side of a buffer, and when there
 * are very many lines, files or levels of directories.
 *
 * Environment: WS_STRESS=1 makes the large inputs ten times larger still.
 */

#include "testtools.h"

#define LONG_TIMEOUT 300000

static unsigned long long rngState = 88172645463325252ULL;

static unsigned rnd32(void) {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 7;
    rngState ^= rngState << 17;
    return (unsigned)(rngState >> 11);
}

static int stress;

/** Runs a tool with a file as standard input; the output is in r, with its length. */
static void run_file(const char* tool, const char* options, const char* file, RunResult* r) {
    char path[MAX_PATH * 2], inputPath[MAX_PATH * 2];
    bin_path(tool, path, sizeof path);
    snprintf(inputPath, sizeof inputPath, "%s\\%s", workDir, file);
    CHECK(run_program_ex(path, options, NULL, 0, inputPath, workDir, LONG_TIMEOUT, r) == 0);
    CHECK(!r->timedOut);
}

static void run_args(const char* tool, const char* options, RunResult* r) {
    char path[MAX_PATH * 2];
    bin_path(tool, path, sizeof path);
    CHECK(run_program_ex(path, options, NULL, 0, NULL, workDir, LONG_TIMEOUT, r) == 0);
    CHECK(!r->timedOut);
}

/** The number at the start of the output: wc prints counts. */
static unsigned long long first_number(const RunResult* r) {
    return (unsigned long long)_strtoui64(r->out, NULL, 10);
}

static unsigned long long line_hash(const char* start, size_t length) {
    unsigned long long h = 1469598103934665603ULL;
    size_t i;
    for (i = 0; i < length; i++) {
        h ^= (unsigned char)start[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* ---- sort of a very large input ----------------------------------------------------- */

static void test_sort_scale(void) {
    size_t lines = stress ? 5000000 : 500000, i;
    char path[MAX_PATH * 2];
    FILE* file;
    unsigned long long sumIn = 0, sumOut = 0;
    RunResult r;
    size_t offset, lineCount;
    int ordered = 1;

    printf("sort: %lu lines\n", (unsigned long)lines);

    snprintf(path, sizeof path, "%s\\unsorted.txt", workDir);
    file = fopen(path, "wb");
    for (i = 0; i < lines; i++) {
        unsigned value = rnd32() % (unsigned)(lines / 2 + 1);       // many equal lines
        char line[16];
        int n = snprintf(line, sizeof line, "%09u\n", value);
        sumIn += line_hash(line, (size_t)n);
        fwrite(line, 1, (size_t)n, file);
    }
    fclose(file);

    run_file("sort", "", "unsorted.txt", &r);
    CHECK_INT(r.exitCode, 0);
    CHECK_INT(r.outLength, lines * 10);

    // sorted, and the same lines as the input
    lineCount = 0;
    for (offset = 0; offset + 10 <= r.outLength; offset += 10) {
        sumOut += line_hash(r.out + offset, 10);
        if (offset > 0 && memcmp(r.out + offset - 10, r.out + offset, 9) > 0)
            ordered = 0;
        lineCount++;
    }
    CHECK_INT(lineCount, lines);
    CHECK(ordered);
    CHECK(sumIn == sumOut);
    run_result_free(&r);

    // -u keeps one of each, which is as many as there are different values
    {
        unsigned char* seen = calloc(lines / 2 + 2, 1);
        size_t distinct = 0;
        char* text;
        size_t length, pos;
        text = slurp_n(path, &length);
        for (pos = 0; pos + 10 <= length; pos += 10) {
            unsigned long v = strtoul(text + pos, NULL, 10);
            if (!seen[v]) {
                seen[v] = 1;
                distinct++;
            }
        }
        free(text);
        free(seen);

        run_file("sort", "-u", "unsorted.txt", &r);
        CHECK_INT(r.outLength, distinct * 10);
        run_result_free(&r);
    }

    // reverse numeric, which the checker above does not cover
    run_file("sort", "-rn", "unsorted.txt", &r);
    CHECK_INT(r.outLength, lines * 10);
    ordered = 1;
    for (offset = 10; offset + 10 <= r.outLength; offset += 10)
        if (memcmp(r.out + offset - 10, r.out + offset, 9) < 0)
            ordered = 0;
    CHECK(ordered);
    run_result_free(&r);

    // the sorted output is accepted by -c
    {
        RunResult sorted;
        run_file("sort", "", "unsorted.txt", &sorted);
        write_file(workDir, "sorted.txt", sorted.out, sorted.outLength);
        run_result_free(&sorted);
        run_file("sort", "-c", "sorted.txt", &r);
        CHECK_INT(r.exitCode, 0);
        run_result_free(&r);
        run_file("sort", "-c", "unsorted.txt", &r);
        CHECK_INT(r.exitCode, 1);
        run_result_free(&r);
    }
}

/* ---- one line as long as the file --------------------------------------------------- */

static void test_long_line(void) {
    size_t size = stress ? 200000000 : 20000000;
    char path[MAX_PATH * 2];
    FILE* file;
    char block[65536];
    size_t written;
    RunResult r;

    printf("one line of %lu bytes, without a line feed\n", (unsigned long)size);

    memset(block, 'x', sizeof block);
    snprintf(path, sizeof path, "%s\\longline.txt", workDir);
    file = fopen(path, "wb");
    for (written = 0; written < size; written += sizeof block)
        fwrite(block, 1, size - written < sizeof block ? size - written : sizeof block, file);
    fclose(file);

    run_file("wc", "-c", "longline.txt", &r);
    CHECK_INT(first_number(&r), size);
    run_result_free(&r);
    run_file("wc", "-lwL", "longline.txt", &r);
    {
        unsigned long long l = 99, w = 99, m = 99;
        CHECK(sscanf(r.out, "%llu %llu %llu", &l, &w, &m) == 3);
        CHECK_INT(l, 0);
        CHECK_INT(w, 1);
        CHECK_INT(m, size);
    }
    run_result_free(&r);

    run_file("cat", "", "longline.txt", &r);
    CHECK_INT(r.outLength, size);
    run_result_free(&r);

    run_file("head", "-n 1", "longline.txt", &r);
    CHECK_INT(r.outLength, size);
    run_result_free(&r);

    run_file("head", "-c 10", "longline.txt", &r);
    CHECK_STR(r.out, "xxxxxxxxxx");
    run_result_free(&r);
    run_file("tail", "-c 10", "longline.txt", &r);
    CHECK_STR(r.out, "xxxxxxxxxx");
    run_result_free(&r);
    run_file("tail", "-n 1", "longline.txt", &r);
    CHECK_INT(r.outLength, size);
    run_result_free(&r);

    // a line feed is added to the last line, as GNU sort and uniq do
    run_file("sort", "", "longline.txt", &r);
    CHECK_INT(r.outLength, size + 1);
    CHECK(r.out[size] == '\n');
    run_result_free(&r);
    run_file("uniq", "", "longline.txt", &r);
    CHECK_INT(r.outLength, size + 1);
    run_result_free(&r);

    run_file("cut", "-c1-5", "longline.txt", &r);
    CHECK_STR(r.out, "xxxxx\n");
    run_result_free(&r);
    run_file("cut", "-b-3,5-", "longline.txt", &r);
    CHECK_INT(r.outLength, size - 1 + 1);
    run_result_free(&r);

    run_file("tr", "x y", "longline.txt", &r);
    CHECK_INT(r.outLength, size);
    CHECK(r.out[0] == 'y' && r.out[size - 1] == 'y' && r.out[size / 2] == 'y');
    run_result_free(&r);
}

/* ---- sizes on either side of the buffers (65536 bytes) ---------------------------- */

static void test_buffer_boundaries(void) {
    static const size_t sizes[] = { 1, 4095, 4096, 4097, 65535, 65536, 65537, 131071, 131072, 131073, 262144 };
    size_t s, k;

    printf("sizes around the buffers\n");

    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        size_t size = sizes[s];
        unsigned char* data = malloc(size);
        RunResult r;
        char options[64];
        static const long long cuts[] = { 1, 0, 0, 0 };

        // all byte values, in an order that no line is made of
        for (k = 0; k < size; k++)
            data[k] = (unsigned char)((k * 7 + k / 256) & 0xFF);
        // no line feed, so that the tools see one line; a NUL is in it as well
        for (k = 0; k < size; k++)
            if (data[k] == '\n' || data[k] == '\r')
                data[k] = 'n';
        write_file(workDir, "boundary.bin", data, size);

        run_file("cat", "", "boundary.bin", &r);
        CHECK_INT(r.outLength, size);
        CHECK(r.outLength == size && memcmp(r.out, data, size) == 0);
        run_result_free(&r);

        run_file("wc", "-c", "boundary.bin", &r);
        CHECK_INT(first_number(&r), size);
        run_result_free(&r);

        // the last bytes, and the first, at the sizes around the size of the file
        {
            long long ns[4];
            int n;
            ns[0] = 1;
            ns[1] = (long long)size - 1;
            ns[2] = (long long)size;
            ns[3] = (long long)size + 1;
            (void)cuts;
            for (n = 0; n < 4; n++) {
                size_t want = ns[n] > (long long)size ? size : (size_t)ns[n];

                snprintf(options, sizeof options, "-c %lld", ns[n]);
                run_file("tail", options, "boundary.bin", &r);
                CHECK(r.outLength == want && (want == 0 || memcmp(r.out, data + size - want, want) == 0));
                run_result_free(&r);

                run_file("head", options, "boundary.bin", &r);
                CHECK(r.outLength == want && (want == 0 || memcmp(r.out, data, want) == 0));
                run_result_free(&r);
            }
        }

        // from the middle of the file to its end
        snprintf(options, sizeof options, "-c +%lu", (unsigned long)(size / 2 + 1));
        run_file("tail", options, "boundary.bin", &r);
        CHECK(r.outLength == size - size / 2 && memcmp(r.out, data + size / 2, size - size / 2) == 0);
        run_result_free(&r);

        free(data);
    }

    // a line feed on the last byte before a buffer ends, on its last byte, and on the first after it
    {
        static const size_t at[] = { 65534, 65535, 65536, 131071, 131072 };
        size_t a;
        for (a = 0; a < sizeof at / sizeof at[0]; a++) {
            size_t size = at[a] + 100;
            char* data = malloc(size + 1);
            RunResult r;

            memset(data, 'a', size);
            data[at[a]] = '\n';                 // the first line is at[a] bytes, then the line feed
            data[size - 1] = '\n';              // and a second line
            write_file(workDir, "lf-boundary.txt", data, size);

            run_file("head", "-n 1", "lf-boundary.txt", &r);
            CHECK_INT(r.outLength, at[a] + 1);
            run_result_free(&r);
            run_file("tail", "-n 1", "lf-boundary.txt", &r);
            CHECK_INT(r.outLength, size - at[a] - 1);
            run_result_free(&r);
            run_file("tail", "-n +2", "lf-boundary.txt", &r);
            CHECK_INT(r.outLength, size - at[a] - 1);
            run_result_free(&r);
            run_file("wc", "-l", "lf-boundary.txt", &r);
            CHECK_INT(first_number(&r), 2);
            run_result_free(&r);
            run_file("cut", "-c1", "lf-boundary.txt", &r);
            CHECK_STR(r.out, "a\na\n");
            run_result_free(&r);
            run_file("sort", "-u", "lf-boundary.txt", &r);
            CHECK_INT(r.outLength, size);          // two lines of different length, both kept
            run_result_free(&r);

            // the same with CR LF: the CR is the byte before the line feed
            data[at[a] - 1] = '\r';
            data[size - 2] = '\r';
            write_file(workDir, "crlf-boundary.txt", data, size);
            run_file("head", "-n 1", "crlf-boundary.txt", &r);
            CHECK_INT(r.outLength, at[a] + 1);
            run_result_free(&r);
            run_file("wc", "-lL", "crlf-boundary.txt", &r);
            {
                unsigned long long l = 99, w = 99;
                CHECK(sscanf(r.out, "%llu %llu", &l, &w) == 2);
                CHECK_INT(l, 2);
                CHECK_INT(w, at[a] - 1 > size - at[a] - 3 ? at[a] - 1 : size - at[a] - 3);   // the longer line, without its CR
            }
            run_result_free(&r);
            free(data);
        }
    }
}

/* ---- very many lines --------------------------------------------------------------- */

static void test_many_lines(void) {
    size_t lines = stress ? 20000000 : 2000000;
    char* data = malloc(lines);
    RunResult r;
    char expected[64];

    printf("%lu empty lines\n", (unsigned long)lines);
    memset(data, '\n', lines);
    write_file(workDir, "empty-lines.txt", data, lines);
    free(data);

    run_file("wc", "-l", "empty-lines.txt", &r);
    CHECK_INT(first_number(&r), lines);
    run_result_free(&r);

    run_file("sort", "-u", "empty-lines.txt", &r);
    CHECK_STR(r.out, "\n");
    run_result_free(&r);

    snprintf(expected, sizeof expected, "%7lu \n", (unsigned long)lines);
    run_file("uniq", "-c", "empty-lines.txt", &r);
    CHECK_STR(r.out, expected);
    run_result_free(&r);

    run_file("head", "-n 3", "empty-lines.txt", &r);
    CHECK_STR(r.out, "\n\n\n");
    run_result_free(&r);

    run_file("tail", "-n 2", "empty-lines.txt", &r);
    CHECK_STR(r.out, "\n\n");
    run_result_free(&r);

    run_file("cat", "-s", "empty-lines.txt", &r);
    CHECK_STR(r.out, "\n");
    run_result_free(&r);

    // all of them numbered
    run_file("cat", "-n", "empty-lines.txt", &r);
    snprintf(expected, sizeof expected, "%6lu\t\n", (unsigned long)lines);
    CHECK(r.outLength >= strlen(expected) &&
          memcmp(r.out + r.outLength - strlen(expected), expected, strlen(expected)) == 0);
    run_result_free(&r);

    run_file("tr", "-d \"\\n\"", "empty-lines.txt", &r);
    CHECK_INT(r.outLength, 0);
    run_result_free(&r);
}

/* ---- paths, and many files -------------------------------------------------------- */

static void test_paths(void) {
    char dir[MAX_PATH * 2];
    RunResult r;
    char arguments[1200], deep[900];
    int i;

    printf("deep and long paths, many files\n");

    snprintf(dir, sizeof dir, "%s\\paths", workDir);
    reset_dir(dir);

    // 25 levels, short names: well inside the limit of 260 characters
    deep[0] = '\0';
    for (i = 0; i < 25; i++)
        strcat(deep, i == 0 ? "d1" : "\\d1");
    snprintf(arguments, sizeof arguments, "-p %s", deep);
    run_tool(dir, "mkdir", arguments, NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    snprintf(arguments, sizeof arguments, "%s\\file.txt", deep);
    run_tool(dir, "touch", arguments, NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    run_tool(dir, "cp", "-r d1 copy", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    run_result_free(&r);

    // twenty-six directories and the file: ls -R lists each directory with a header
    run_tool(dir, "ls", "-R copy", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(strstr(r.out, "file.txt") != NULL);
    run_result_free(&r);

    run_tool(dir, "rm", "-r d1 copy", NULL, &r);
    CHECK_INT(r.exitCode, 0);
    CHECK(!path_exists_in(dir, "d1") && !path_exists_in(dir, "copy"));
    run_result_free(&r);

    // a long name is fine, as long as the whole path stays below the 260 characters of Windows
    {
        char name[260];
        memset(name, 'n', 100);
        name[100] = '\0';
        snprintf(arguments, sizeof arguments, "%s", name);
        run_tool(dir, "touch", arguments, NULL, &r);
        CHECK_INT(r.exitCode, 0);
        CHECK(path_exists_in(dir, name));
        run_result_free(&r);
        snprintf(arguments, sizeof arguments, "-l %s", name);
        run_tool(dir, "ls", arguments, NULL, &r);
        CHECK_INT(r.exitCode, 0);
        run_result_free(&r);
    }

    // a path of 400 characters is beyond what Windows takes: an error, not a crash
    {
        char longPath[512];
        longPath[0] = '\0';
        for (i = 0; i < 40; i++)
            strcat(longPath, i == 0 ? "abcdefghi" : "\\abcdefghi");
        snprintf(arguments, sizeof arguments, "-p %s", longPath);
        run_tool(dir, "mkdir", arguments, NULL, &r);
        CHECK_INT(r.exitCode, 1);
        CHECK(strstr(r.err, "mkdir:") != NULL);
        CHECK(strstr(r.err, "memory") == NULL);
        run_result_free(&r);
        snprintf(arguments, sizeof arguments, "%s", longPath);
        run_tool(dir, "cat", arguments, NULL, &r);
        CHECK_INT(r.exitCode, 1);
        run_result_free(&r);
        run_tool(dir, "rm", arguments, NULL, &r);
        CHECK_INT(r.exitCode, 1);
        run_result_free(&r);
    }

    // very many files: listed, counted and removed through the shell with a wildcard
    {
        int files = stress ? 20000 : 2500;
        char many[MAX_PATH * 2];

        snprintf(many, sizeof many, "%s\\many", dir);
        CreateDirectoryA(many, NULL);
        for (i = 0; i < files; i++) {
            char name[32];
            snprintf(name, sizeof name, "f%d.d", i);
            write_text(many, name, "x");
        }

        run_tool(many, "ls", "", NULL, &r);
        {
            size_t lines = 0;
            const char* p;
            for (p = r.out; *p != '\0'; p++)
                if (*p == '\n')
                    lines++;
            CHECK_INT(lines, files);
        }
        run_result_free(&r);

        // the command line of the shell has a limit of its own: 2500 names fit, so the files are counted
        if (!stress) {
            RunResult shell;
            run_program(shellPath, "", "cd paths\\many\nls f*.d | wc -l\n", workDir, &shell);
            CHECK(strstr(shell.out, "2500") != NULL);
            run_result_free(&shell);
        }

        run_tool(dir, "rm", "-r many", NULL, &r);
        CHECK_INT(r.exitCode, 0);
        CHECK(!path_exists_in(dir, "many"));
        run_result_free(&r);
    }
}

/* ---- keys that do not make sense -------------------------------------------------------- */

static void test_odd_keys(void) {
    RunResult r;
    static const char* keys[] = { "-k99999", "-k1.99999", "-k2,1", "-k1,1.0", "-k3,3 -k1,1n", "-t: -k5", "-k1b,1b",
                                  "-k1.1,1.1", "-kx", "-k0", "-k1,0" };
    size_t k;

    printf("odd keys\n");

    write_text(workDir, "keys.txt", "b:2\na:10\nc\n\nd:x:y\n");
    for (k = 0; k < sizeof keys / sizeof keys[0]; k++) {
        char options[64];
        snprintf(options, sizeof options, "%s keys.txt", keys[k]);
        run_args("sort", options, &r);
        // valid keys sort the five lines, invalid ones are refused; nothing crashes or hangs
        if (r.exitCode == 0) {
            size_t lines = 0, i;
            for (i = 0; i < r.outLength; i++)
                if (r.out[i] == '\n')
                    lines++;
            CHECK_INT(lines, 5);
        } else {
            CHECK_INT(r.exitCode, 2);
        }
        CHECK(!r.timedOut);
        run_result_free(&r);
    }
}

int main(void) {
    const char* value = getenv("WS_STRESS");
    stress = value != NULL && value[0] != '\0' && value[0] != '0';

    tools_init("limits_test");
    if (stress)
        printf("WS_STRESS: ten times larger inputs\n");

    test_buffer_boundaries();
    test_many_lines();
    test_long_line();
    test_sort_scale();
    test_paths();
    test_odd_keys();
    return test_summary("limits_test");
}
