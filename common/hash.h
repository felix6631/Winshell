#ifndef HASH_H
#define HASH_H

#include <stddef.h>
#include <stdint.h>

/*
 * The message digests of the *sum tools: MD5 (RFC 1321), SHA-1, SHA-224, SHA-256, SHA-384 and SHA-512
 * (FIPS 180-4), written out here so that the tools need nothing from Windows. One context, whatever the algorithm.
 */

typedef enum { HASH_MD5, HASH_SHA1, HASH_SHA224, HASH_SHA256, HASH_SHA384, HASH_SHA512 } HashAlgorithm;

typedef struct {
    HashAlgorithm algorithm;
    uint64_t      length;           /* bytes so far */
    unsigned char block[128];
    size_t        blockLength;
    union {
        uint32_t w32[8];
        uint64_t w64[8];
    } state;
} HashContext;

/** The size of the digest in bytes: 16, 20, 28, 32, 48, 64. */
size_t hash_size(HashAlgorithm algorithm);

void hash_init(HashContext* context, HashAlgorithm algorithm);
void hash_update(HashContext* context, const void* data, size_t length);

/** Ends the digest; `out` has room for hash_size(). */
void hash_final(HashContext* context, unsigned char* out);

#endif //HASH_H
