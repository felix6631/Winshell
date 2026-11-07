/*
 * base32: encodes and decodes base32 (RFC 4648), as GNU base32 does. The work is in common/basecode.c, which
 * base64 uses as well.
 */

#include "basecode.h"

int main(int argc, char* argv[]) {
    return basecode_main("base32", "0.1", 32, argc, argv);
}
