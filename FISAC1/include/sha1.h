/*
    sha1.h - header for a simple SHA1 implementation

    This is free and unencumbered software released into the public domain.
*/

#ifndef SHA1_H
#define SHA1_H

#include <stdint.h>

#define SHA1_BLOCK_SIZE 64
#define SHA1_DIGEST_SIZE 20

typedef struct {
    uint8_t block[SHA1_BLOCK_SIZE];
    uint32_t state[5];
    uint64_t byte_count;
    uint8_t buffer_offset;
} SHA1_CTX;

void sha1_init(SHA1_CTX *ctx);
void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len);
void sha1_final(SHA1_CTX *ctx, uint8_t *digest);

#endif // SHA1_H
