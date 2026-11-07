/*
 * sha256sum: prints and checks SHA256 checksums, as GNU sha256sum does. The work is in common/checksum.c and common/hash.c.
 */

#include "checksum.h"

int main(int argc, char* argv[]) {
    return checksum_main("sha256sum", "0.1", "SHA256", HASH_SHA256, argc, argv);
}
