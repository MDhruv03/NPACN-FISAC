/*
    base64.c - a simple base64 implementation

    This is free and unencumbered software released into the public domain.
*/

#include "base64.h"

static const unsigned char base64_table[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64_encode(const unsigned char *src, size_t len, unsigned char *out) {
    unsigned char *d = out;
    const unsigned char *s = src;
    const unsigned char *end = src + len;
    
    while (end - s >= 3) {
        *d++ = base64_table[s[0] >> 2];
        *d++ = base64_table[((s[0] & 0x03) << 4) | (s[1] >> 4)];
        *d++ = base64_table[((s[1] & 0x0f) << 2) | (s[2] >> 6)];
        *d++ = base64_table[s[2] & 0x3f];
        s += 3;
    }

    if (end - s > 0) {
        *d++ = base64_table[s[0] >> 2];
        if (end - s == 1) {
            *d++ = base64_table[(s[0] & 0x03) << 4];
            *d++ = '=';
        } else {
            *d++ = base64_table[((s[0] & 0x03) << 4) | (s[1] >> 4)];
            *d++ = base64_table[(s[1] & 0x0f) << 2];
        }
        *d++ = '=';
    }

    *d = '\0';
    return d - out;
}
