/*
    base64.h - header for a simple base64 implementation

    This is free and unencumbered software released into the public domain.
*/

#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>

size_t base64_encode(const unsigned char *src, size_t len, unsigned char *out);

#endif // BASE64_H
