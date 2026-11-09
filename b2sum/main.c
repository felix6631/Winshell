/*
 * b2sum: prints and checks BLAKE2b checksums (RFC 7693), as GNU b2sum does, also with a shorter digest (-l). The work
 * is in common/checksum.c and common/hash.c.
 */

#include "checksum.h"

int main(int argc, char* argv[]) {
    return checksum_main("b2sum", "0.1", "BLAKE2b", HASH_BLAKE2B, argc, argv);
}
