/*
 * md5sum: prints and checks MD5 checksums, as GNU md5sum does. The work is in common/checksum.c and common/hash.c.
 */

#include "checksum.h"

int main(int argc, char* argv[]) {
    return checksum_main("md5sum", "0.1", "MD5", HASH_MD5, argc, argv);
}
