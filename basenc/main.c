/*
 * basenc: encodes and decodes the encodings of base64 and base32 (RFC 4648) and more (base64url, base32hex, base16, bit
 * strings, Z85), as GNU basenc does. The work is in common/basecode.c, which base64 and base32 use as well.
 */

#include "basecode.h"

int main(int argc, char* argv[]) {
    return basenc_main(argc, argv);
}
