/*
 * sum: prints the old checksum (BSD, 16 bits and 1024-byte blocks, or System V with -s) and the number of blocks
 * of each file, as GNU sum does. The name follows when there is more than one operand.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "textutil.h"

#define VERSION "0.1"
#define PROGRAM "sum"

static void print_usage(void) {
    puts("Usage: sum [OPTION]... [FILE]...\n"
         "Print checksum and block counts for each FILE.\n"
         "\n"
         "  -r              use BSD sum algorithm, use 1K blocks\n"
         "  -s, --sysv      use System V sum algorithm, use 512 bytes blocks\n"
         "      --help      display this help and exit\n"
         "      --version   output version information and exit\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.");
}

enum { OPT_BSD = 1, OPT_SYSV };

static const ArgsOption options[] = {
    { OPT_BSD,  'r', NULL,   ARGS_NONE },
    { OPT_SYSV, 's', "sysv", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    int* sysv = context;

    (void)value;
    *sysv = id == OPT_SYSV;
    return 0;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, i, status = 0, sysv = 0, printName;
    unsigned char buffer[65536];

    text_binary_streams();
    result = args_parse(&spec, argc, argv, on_option, &sysv, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    printName = count > 1 || (sysv && count > 0);    // System V writes the name of a single operand too
    count = (int)text_default_operands(operands, (size_t)count);

    for (i = 0; i < count; i++) {
        FILE* in;
        unsigned long long size = 0, blocks, total = 0;
        unsigned checksum = 0;
        long got;

        if (text_open(PROGRAM, operands[i], 1, &in) != 0) {
            status = 1;
            continue;
        }
        while ((got = text_read_some(in, buffer, sizeof buffer)) > 0) {
            long k;

            for (k = 0; k < got; k++) {
                if (sysv) {
                    total += buffer[k];
                } else {
                    checksum = (checksum >> 1) + ((checksum & 1) << 15);
                    checksum += buffer[k];
                    checksum &= 0xFFFF;
                }
            }
            size += (unsigned long long)got;
        }
        if (in != stdin)
            fclose(in);
        if (got < 0) {
            fprintf(stderr, PROGRAM ": %s: read error\n", operands[i]);
            status = 1;
            continue;
        }
        if (sysv) {
            unsigned long long r = (total & 0xFFFF) + ((total & 0xFFFFFFFFull) >> 16);

            r = (r & 0xFFFF) + (r >> 16);
            blocks = (size + 511) / 512;
            printf("%llu %llu", r, blocks);
        } else {
            blocks = (size + 1023) / 1024;
            printf("%05u %5llu", checksum, blocks);
        }
        if (printName)
            printf(" %s", operands[i]);
        putchar('\n');
    }
    return fflush(stdout) != 0 ? 1 : status;
}
