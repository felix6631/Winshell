#ifndef BASECODE_H
#define BASECODE_H

/**
 * The whole of base64 and base32 (RFC 4648): the command line, the encoding in lines of 76 characters (-w),
 * and the decoding (-d, -i). `bits` is 64 or 32; `program` the name in messages. Returns the exit status.
 */
int basecode_main(const char* program, const char* version, int bits, int argc, char* argv[]);

#endif //BASECODE_H
