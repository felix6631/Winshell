/*
 * sha512sum: prints and checks SHA512 checksums, as GNU sha512sum does. The work is in common/checksum.c and common/hash.c.
 */

#include "checksum.h"

int main(int argc, char* argv[]) {
    return checksum_main("sha512sum", "0.1", "SHA512", HASH_SHA512, argc, argv);
}
