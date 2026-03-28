/*
    sha1.c - a simple SHA1 implementation

    This is free and unencumbered software released into the public domain.
*/

#include "sha1.h"
#include <string.h>

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_transform(SHA1_CTX *ctx) {
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    int t;

    for (t = 0; t < 16; ++t) {
        w[t] = ((uint32_t)ctx->block[t * 4]) << 24;
        w[t] |= ((uint32_t)ctx->block[t * 4 + 1]) << 16;
        w[t] |= ((uint32_t)ctx->block[t * 4 + 2]) << 8;
        w[t] |= ctx->block[t * 4 + 3];
    }

    for (t = 16; t < 80; ++t) {
        w[t] = ROTATE_LEFT(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    for (t = 0; t < 80; ++t) {
        uint32_t temp = ROTATE_LEFT(a, 5) + e + w[t];
        if (t < 20) {
            temp += ((b & c) | ((~b) & d)) + 0x5A827999;
        } else if (t < 40) {
            temp += (b ^ c ^ d) + 0x6ED9EBA1;
        } else if (t < 60) {
            temp += ((b & c) | (b & d) | (c & d)) + 0x8F1BBCDC;
        } else {
            temp += (b ^ c ^ d) + 0xCA62C1D6;
        }
        e = d;
        d = c;
        c = ROTATE_LEFT(b, 30);
        b = a;
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

void sha1_init(SHA1_CTX *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->byte_count = 0;
    ctx->buffer_offset = 0;
}

void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len) {
    ctx->byte_count += len;
    const uint8_t *end = data + len;
    while (data < end) {
        uint8_t copy_len = SHA1_BLOCK_SIZE - ctx->buffer_offset;
        if (copy_len > len) {
            copy_len = len;
        }
        memcpy(ctx->block + ctx->buffer_offset, data, copy_len);
        data += copy_len;
        ctx->buffer_offset += copy_len;
        if (ctx->buffer_offset == SHA1_BLOCK_SIZE) {
            sha1_transform(ctx);
            ctx->buffer_offset = 0;
        }
    }
}

void sha1_final(SHA1_CTX *ctx, uint8_t *digest) {
    uint8_t padding[SHA1_BLOCK_SIZE * 2];
    uint64_t bit_count = ctx->byte_count * 8;
    int i;

    uint8_t pad_len = (ctx->buffer_offset < 56) ? (56 - ctx->buffer_offset) : (120 - ctx->buffer_offset);
    memset(padding, 0, pad_len);
    padding[0] = 0x80;

    sha1_update(ctx, padding, pad_len);

    for (i = 0; i < 8; ++i) {
        ctx->block[56 + i] = (uint8_t)(bit_count >> (56 - i * 8));
    }
    sha1_transform(ctx);

    for (i = 0; i < 5; ++i) {
        digest[i * 4] = (ctx->state[i] >> 24) & 0xFF;
        digest[i * 4 + 1] = (ctx->state[i] >> 16) & 0xFF;
        digest[i * 4 + 2] = (ctx->state[i] >> 8) & 0xFF;
        digest[i * 4 + 3] = ctx->state[i] & 0xFF;
    }
}
