/*
    websocket.c - WebSocket protocol implementation over WinSock2.

    Implements the WebSocket handshake (RFC 6455) and frame-level I/O.
    The handshake upgrades a standard HTTP connection to a full-duplex
    WebSocket connection using SHA-1 hashing and Base64 encoding.

    Frame handling supports:
    - Text frames (opcode 0x01) for JSON messages
    - Close frames (opcode 0x08) for graceful termination
    - Masked payloads (required for client-to-server messages)
    - Extended payload lengths (126 and 127 modes)
*/

#include "websocket.h"
#include "sha1.h"
#include "base64.h"
#include "network.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* Read exactly len bytes; tolerate transient WSAEWOULDBLOCK from robust_recv. */
static int recv_exact(SOCKET client_sock, char *buf, int len) {
    int total = 0;
    int idle_retries = 0;

    while (total < len) {
        int n = robust_recv(client_sock, buf + total, len - total);
        if (n > 0) {
            total += n;
            idle_retries = 0;
            continue;
        }

        if (n == -1) {
            /* Would-block on non-blocking sockets. Retry briefly. */
            if (++idle_retries > 2000) {
                return -1;
            }
            Sleep(1);
            continue;
        }

        /* n == 0 (disconnect) or n == -2 (fatal recv error) */
        return -1;
    }

    return total;
}

/*
 * Manual 64-bit big-endian byte swap for Windows.
 * Windows does not provide be64toh() / htobe64() like Linux.
 * We use ntohll() if available, otherwise implement manually.
 */
static uint64_t swap_uint64(uint64_t val) {
    val = ((val << 8) & 0xFF00FF00FF00FF00ULL) | ((val >> 8) & 0x00FF00FF00FF00FFULL);
    val = ((val << 16) & 0xFFFF0000FFFF0000ULL) | ((val >> 16) & 0x0000FFFF0000FFFFULL);
    return (val << 32) | (val >> 32);
}

/*
 * websocket_handshake: Performs the WebSocket upgrade handshake.
 *
 * Protocol flow:
 * 1. Client sends HTTP GET with "Upgrade: websocket" and Sec-WebSocket-Key
 * 2. Server concatenates the key with the GUID
 * 3. Server computes SHA-1 hash of the concatenated string
 * 4. Server Base64-encodes the hash
 * 5. Server responds with HTTP 101 Switching Protocols
 *
 * Returns 0 on success, -1 on failure
 */
int websocket_handshake(SOCKET client_sock) {
    char buffer[4096];
    int bytes_received = 0;

    /* Read until end of HTTP headers (\r\n\r\n) to avoid partial-handshake failures. */
    while (bytes_received < (int)sizeof(buffer) - 1) {
        int n = robust_recv(client_sock, buffer + bytes_received, (int)sizeof(buffer) - 1 - bytes_received);
        if (n > 0) {
            bytes_received += n;
            buffer[bytes_received] = '\0';
            if (strstr(buffer, "\r\n\r\n") != NULL) {
                break;
            }
            continue;
        }

        if (n == -1) {
            /* Wait briefly for remaining header bytes on non-blocking sockets. */
            Sleep(1);
            continue;
        }

        return -1;
    }

    if (bytes_received <= 0 || strstr(buffer, "\r\n\r\n") == NULL) {
        fprintf(stderr, "[WS] Handshake failed: incomplete HTTP upgrade request\n");
        return -1;
    }

    buffer[bytes_received] = '\0';

    /* Extract Sec-WebSocket-Key from HTTP headers */
    char *key_start = strstr(buffer, "Sec-WebSocket-Key: ");
    if (!key_start) {
        fprintf(stderr, "[WS] Handshake failed: no Sec-WebSocket-Key found\n");
        return -1;
    }
    key_start += 19;
    char *key_end = strstr(key_start, "\r\n");
    if (!key_end) {
        return -1;
    }
    *key_end = '\0';

    /* Concatenate key with WebSocket GUID */
    char concatenated_key[256];
    snprintf(concatenated_key, sizeof(concatenated_key), "%s%s", key_start, WEBSOCKET_GUID);

    /* SHA-1 hash of the concatenated key */
    uint8_t sha1_digest[SHA1_DIGEST_SIZE];
    SHA1_CTX sha1_ctx;
    sha1_init(&sha1_ctx);
    sha1_update(&sha1_ctx, (uint8_t *)concatenated_key, (int)strlen(concatenated_key));
    sha1_final(&sha1_ctx, sha1_digest);

    /* Base64 encode the SHA-1 digest */
    unsigned char base64_encoded[128];
    base64_encode(sha1_digest, SHA1_DIGEST_SIZE, base64_encoded);

    /* Build and send the HTTP 101 response */
    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n",
             base64_encoded);

    if (robust_send(client_sock, response, (int)strlen(response)) == -1) {
        return -1;
    }

    return 0;
}

/*
 * websocket_frame_recv: Reads and decodes a single WebSocket frame.
 *
 * WebSocket frame structure:
 *   Byte 0: [FIN(1)][RSV(3)][OPCODE(4)]
 *   Byte 1: [MASK(1)][PAYLOAD_LEN(7)]
 *   If PAYLOAD_LEN == 126: next 2 bytes are extended length (big-endian)
 *   If PAYLOAD_LEN == 127: next 8 bytes are extended length (big-endian)
 *   If MASK == 1: next 4 bytes are masking key
 *   Remaining bytes: payload data
 *
 * Returns payload length on success, -1 on error/disconnect
 */
int websocket_frame_recv(SOCKET client_sock, char *buffer, int buffer_size) {
    uint8_t header[2];
    if (recv_exact(client_sock, (char *)header, 2) <= 0) return -1;

    uint8_t opcode = header[0] & 0x0F;
    uint8_t mask = (header[1] >> 7) & 1;
    uint64_t payload_len = header[1] & 0x7F;

    /* Handle close frame (opcode 8) - graceful WebSocket termination */
    if (opcode == 8) {
        return -1;
    }

    /* Extended payload length handling */
    if (payload_len == 126) {
        uint16_t len;
        if (recv_exact(client_sock, (char *)&len, 2) <= 0) return -1;
        payload_len = ntohs(len);
    } else if (payload_len == 127) {
        uint64_t len;
        if (recv_exact(client_sock, (char *)&len, 8) <= 0) return -1;
        payload_len = swap_uint64(len);
    }

    if (payload_len > (uint64_t)(buffer_size - 1)) {
        fprintf(stderr, "[WS] Payload too large: %llu > %d\n",
                (unsigned long long)payload_len, buffer_size - 1);
        return -1;
    }

    /* Read masking key (client-to-server frames MUST be masked per RFC 6455) */
    uint8_t masking_key[4];
    if (mask) {
        if (recv_exact(client_sock, (char *)masking_key, 4) <= 0) return -1;
    }

    /* Read payload data */
    int to_read = (int)payload_len;
    if (recv_exact(client_sock, buffer, to_read) <= 0) return -1;

    /* Unmask payload */
    if (mask) {
        for (int i = 0; i < to_read; i++) {
            buffer[i] ^= masking_key[i % 4];
        }
    }
    buffer[to_read] = '\0';

    /* Respond to ping and ignore non-text control/data frames gracefully. */
    if (opcode == 9) { /* PING */
        (void)websocket_frame_send(client_sock, buffer, payload_len, 10); /* PONG */
        return 0;
    }

    if (opcode == 10) { /* PONG */
        return 0;
    }

    if (opcode != 1) {
        return 0;
    }

    return to_read;
}

/*
 * websocket_frame_send: Constructs and sends a WebSocket frame.
 *
 * Server-to-client frames are NOT masked (per RFC 6455).
 * Handles all payload length modes (7-bit, 16-bit extended, 64-bit extended).
 *
 * Returns 0 on success, -1 on error
 */
int websocket_frame_send(SOCKET client_sock, const char *message, uint64_t len, int type) {
    uint8_t header[10];
    header[0] = 0x80 | (type & 0x0F); /* FIN bit set + opcode */

    int header_len = 2;
    if (len <= 125) {
        header[1] = (uint8_t)len;
    } else if (len <= 65535) {
        header[1] = 126;
        uint16_t n_len = htons((uint16_t)len);
        memcpy(&header[2], &n_len, 2);
        header_len = 4;
    } else {
        header[1] = 127;
        uint64_t n_len = swap_uint64(len);
        memcpy(&header[2], &n_len, 8);
        header_len = 10;
    }

    if (robust_send(client_sock, (char *)header, header_len) == -1) return -1;
    if (robust_send(client_sock, message, (int)len) == -1) return -1;

    return 0;
}
