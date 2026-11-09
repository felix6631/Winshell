/*
 * Tests of df, run as the program it is: the header and the columns for the options, the sizes of a drive in blocks and
 * with -h, that the numbers fit together, --output, --total, -t / -x and the errors. The numbers are those of the
 * disk, so what is checked is how they fit. GNU df is compared with by gnu_compare.sh.
 */

#include "testtools.h"

/** The output of df with these arguments; the caller frees it. NULL on a failure. */
static char* df_output(const char* arguments, int* status) {
    RunResult r;
    char* copy;

    run_tool(workDir, "df", arguments, "", &r);
    if (status != NULL)
        *status = r.exitCode;
    copy = r.out != NULL ? _strdup(r.out) : NULL;
    run_result_free(&r);
    return copy;
}

static void message(const char* arguments, const char* part, int status) {
    RunResult r;

    run_tool(workDir, "df", arguments, "", &r);
    CHECK_INT(r.exitCode, status);
    CHECK(strstr(r.err, part) != NULL);
    run_result_free(&r);
}

static int starts_with(const char* text, const char* prefix) {
    return text != NULL && strncmp(text, prefix, strlen(prefix)) == 0;
}

int main(void) {
    char* text;
    unsigned long long size, used, avail, size2;
    char source[64], target[64], percent[16];
    int status;

    tools_init("df_test");

    printf("the table\n");
    text = df_output("/c", &status);
    CHECK_INT(status, 0);
    CHECK(starts_with(text, "Filesystem     1K-blocks"));
    CHECK(text != NULL && strstr(text, "Used Available Use% Mounted on\n") != NULL);
    CHECK(text != NULL && sscanf(strchr(text, '\n') + 1, "%63s %llu %llu %llu %15s %63s", source, &size, &used, &avail, percent, target) == 6);
    CHECK_STR(source, "C:");
    CHECK_STR(target, "/c");
    CHECK(size >= used && size > 0 && percent[strlen(percent) - 1] == '%');
    free(text);

    printf("the units\n");
    text = df_output("-B1 /c", NULL);
    CHECK(starts_with(text, "Filesystem        1B-blocks") || strstr(text, "1B-blocks") != NULL);
    free(text);
    text = df_output("-B1M /c", NULL);
    CHECK(text != NULL && strstr(text, "1M-blocks") != NULL);
    CHECK(text != NULL && sscanf(strchr(text, '\n') + 1, "%63s %llu", source, &size2) == 2);
    CHECK(size2 > 0 && size2 < size);                                           // megabytes are fewer than kilobytes
    free(text);
    text = df_output("-BM /c", NULL);
    CHECK(text != NULL && strstr(text, "1M-blocks") != NULL);
    CHECK(text != NULL && strchr(strchr(text, '\n') + 1, 'M') != NULL);         // a suffix without a number puts it on the values
    free(text);
    text = df_output("-h /c", NULL);
    CHECK(starts_with(text, "Filesystem      Size  Used Avail Use% Mounted on\n"));
    free(text);
    text = df_output("-H /c", NULL);
    CHECK(starts_with(text, "Filesystem      Size  Used Avail Use% Mounted on\n"));
    free(text);
    text = df_output("-P /c", NULL);
    CHECK(starts_with(text, "Filesystem     1024-blocks      Used Available Capacity Mounted on\n"));
    free(text);
    text = df_output("-kh /c", NULL);
    CHECK(starts_with(text, "Filesystem      Size"));                           // the last of -k and -h wins
    free(text);
    text = df_output("-hk /c", NULL);
    CHECK(starts_with(text, "Filesystem     1K-blocks"));
    free(text);
    text = df_output("-B3 /c", NULL);
    CHECK(text != NULL && strstr(text, "3B-blocks") != NULL);
    free(text);
    text = df_output("-B1000 /c", NULL);
    CHECK(text != NULL && strstr(text, "1kB-blocks") != NULL);
    free(text);

    printf("columns\n");
    text = df_output("-T /c", NULL);
    CHECK(starts_with(text, "Filesystem     Type 1K-blocks"));
    CHECK(text != NULL && strstr(text, "ntfs") != NULL);
    free(text);
    text = df_output("-i /c", NULL);
    CHECK(starts_with(text, "Filesystem     Inodes IUsed IFree IUse% Mounted on\n"));
    CHECK(text != NULL && strstr(text, "C:                  -     -     -     - /c\n") != NULL);
    free(text);
    text = df_output("--output=target,source /c", NULL);
    CHECK_STR(text, "Mounted on Filesystem\n/c         C:\n");
    free(text);
    text = df_output("--output=pcent /c", NULL);
    CHECK(starts_with(text, "Use%\n"));
    free(text);
    text = df_output("--output=file /c", NULL);
    CHECK_STR(text, "File\n/c\n");
    free(text);

    printf("--total, -t, -x\n");
    text = df_output("--total /c", NULL);
    CHECK(text != NULL && strstr(text, "total ") != NULL && strstr(text, " -\n") != NULL);
    free(text);
    text = df_output("-t ntfs /c", &status);
    CHECK(status == 0 || status == 1);
    free(text);
    message("-t nosuchtype /c", "no file systems processed", 1);
    message("-x nosuchtype /c", "", 0);
    message("-t ntfs -x ntfs /c", "file system type 'ntfs' both selected and excluded", 1);

    printf("errors\n");
    message("nosuchfile", "df: nosuchfile: No such file or directory", 1);
    message("/c nosuchfile", "df: nosuchfile: No such file or directory", 1);
    message("-B 0 /c", "invalid -B argument '0'", 1);
    message("-B 1x /c", "invalid suffix in -B argument '1x'", 1);
    message("-B 99999999999999999999 /c", "too large", 1);
    message("--output=bogus /c", "option --output: field 'bogus' unknown", 1);
    message("--output=size,size /c", "field 'size' used more than once", 1);
    message("-i --output /c", "options -i and --output are mutually exclusive", 1);
    expect_run("df", "--version", "", "df (Winshell) 0.1\n", 0);
    return test_summary("df_test");
}
