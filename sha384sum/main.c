/*
 * sha384sum: prints and checks SHA384 checksums, as GNU sha384sum does. The work is in common/checksum.c and common/hash.c.
 */

#include "checksum.h"

int main(int argc, char* argv[]) {
    return checksum_main("sha384sum", "0.1", "SHA384", HASH_SHA384, argc, argv);
}
