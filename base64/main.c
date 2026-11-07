/*
 * base64: encodes and decodes base64 (RFC 4648), as GNU base64 does. The work is in common/basecode.c, which
 * base32 uses as well.
 */

#include "basecode.h"

int main(int argc, char* argv[]) {
    return basecode_main("base64", "0.1", 64, argc, argv);
}
