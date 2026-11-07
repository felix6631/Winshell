/*
 * sha224sum: prints and checks SHA224 checksums, as GNU sha224sum does. The work is in common/checksum.c and common/hash.c.
 */

#include "checksum.h"

int main(int argc, char* argv[]) {
    return checksum_main("sha224sum", "0.1", "SHA224", HASH_SHA224, argc, argv);
}
