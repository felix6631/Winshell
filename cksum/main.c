/*
 * cksum: prints the CRC checksum (the one of POSIX), the size in bytes and the name of each file, as GNU cksum
 * does. Standard input is printed without a name.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "textutil.h"

#define VERSION "0.1"
#define PROGRAM "cksum"

static void print_usage(void) {
    puts("Usage: cksum [FILE]...\n"
         "  or:  cksum [OPTION]\n"
         "Print CRC checksum and byte counts of each FILE.\n"
         "\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit");
}

static const ArgsOption options[] = { { 0 } };
static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    (void)id;
    (void)value;
    (void)context;
    return 0;
}

static uint32_t table[256];

static void make_table(void) {
    uint32_t i, k;

    for (i = 0; i < 256; i++) {
        uint32_t crc = i << 24;

        for (k = 0; k < 8; k++)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
        table[i] = crc;
    }
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, i, status = 0, standardInput;
    unsigned char buffer[65536];

    text_binary_streams();
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    standardInput = count == 0;
    count = (int)text_default_operands(operands, (size_t)count);
    make_table();

    for (i = 0; i < count; i++) {
        FILE* in;
        uint32_t crc = 0;
        unsigned long long size = 0, n;
        long got;

        if (text_open(PROGRAM, operands[i], 1, &in) != 0) {
            status = 1;
            continue;
        }
        while ((got = text_read_some(in, buffer, sizeof buffer)) > 0) {
            long k;

            for (k = 0; k < got; k++)
                crc = (crc << 8) ^ table[((crc >> 24) ^ buffer[k]) & 0xFF];
            size += (unsigned long long)got;
        }
        if (in != stdin)
            fclose(in);
        if (got < 0) {
            fprintf(stderr, PROGRAM ": %s: read error\n", operands[i]);
            status = 1;
            continue;
        }
        for (n = size; n != 0; n >>= 8)
            crc = (crc << 8) ^ table[((crc >> 24) ^ (uint32_t)(n & 0xFF)) & 0xFF];
        crc = ~crc;
        if (standardInput)
            printf("%u %llu\n", (unsigned)crc, size);
        else
            printf("%u %llu %s\n", (unsigned)crc, size, operands[i]);
    }
    return fflush(stdout) != 0 ? 1 : status;
}
