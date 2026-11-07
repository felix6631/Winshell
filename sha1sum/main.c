/*
 * sha1sum: prints and checks SHA1 checksums, as GNU sha1sum does. The work is in common/checksum.c and common/hash.c.
 */

#include "checksum.h"

int main(int argc, char* argv[]) {
    return checksum_main("sha1sum", "0.1", "SHA1", HASH_SHA1, argc, argv);
}
