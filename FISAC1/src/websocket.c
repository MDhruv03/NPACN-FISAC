#include "websocket.h"
#include "sha1.h"
#include "base64.h"
#include "network.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

static const char *WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

int websocket_handshake(int client_sock) {
    char buffer[2048];
    ssize_t bytes_received = robust_recv(client_sock, buffer, sizeof(buffer) - 1);
    if (bytes_received <= 0) {
        return -1;
    }
    buffer[bytes_received] = '\0';

    char *key_start = strstr(buffer, "Sec-WebSocket-Key: ");
    if (!key_start) {
        return -1;
    }
    key_start += 19;
    char *key_end = strstr(key_start, "\r\n");
    if (!key_end) {
        return -1;
    }
    *key_end = '\0';
    
    char concatenated_key[256];
    snprintf(concatenated_key, sizeof(concatenated_key), "%s%s", key_start, WEBSOCKET_GUID);

    uint8_t sha1_digest[SHA1_DIGEST_SIZE];
    SHA1_CTX sha1_ctx;
    sha1_init(&sha1_ctx);
    sha1_update(&sha1_ctx, (uint8_t *)concatenated_key, strlen(concatenated_key));
    sha1_final(&sha1_ctx, sha1_digest);

    unsigned char base64_encoded[128];
    base64_encode(sha1_digest, SHA1_DIGEST_SIZE, base64_encoded);

    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n",
             base64_encoded);

    if (robust_send(client_sock, response, strlen(response)) == -1) {
        return -1;
    }

    return 0;
}

int websocket_frame_recv(int client_sock, char *buffer, int buffer_size) {
    uint8_t header[2];
    if (robust_recv(client_sock, (char*)header, 2) <= 0) return -1;

    uint8_t fin = (header[0] >> 7) & 1;
    uint8_t opcode = header[0] & 0x0F;
    uint8_t mask = (header[1] >> 7) & 1;
    uint64_t payload_len = header[1] & 0x7F;

    if (opcode != 1) return -1; // Only support text frames

    if (payload_len == 126) {
        uint16_t len;
        if (robust_recv(client_sock, (char*)&len, 2) <= 0) return -1;
        payload_len = ntohs(len);
    } else if (payload_len == 127) {
        uint64_t len;
        if (robust_recv(client_sock, (char*)&len, 8) <= 0) return -1;
        payload_len = be64toh(len);
    }

    if (payload_len > buffer_size - 1) return -1;

    uint8_t masking_key[4];
    if (mask) {
        if (robust_recv(client_sock, (char*)masking_key, 4) <= 0) return -1;
    }

    if (robust_recv(client_sock, buffer, payload_len) <= 0) return -1;

    if (mask) {
        for (uint64_t i = 0; i < payload_len; i++) {
            buffer[i] ^= masking_key[i % 4];
        }
    }
    buffer[payload_len] = '\0';

    return payload_len;
}

int websocket_frame_send(int client_sock, const char *message, uint64_t len, int type) {
    uint8_t header[10];
    header[0] = 0x80 | (type & 0x0F); // FIN + opcode

    int header_len = 2;
    if (len <= 125) {
        header[1] = len;
    } else if (len <= 65535) {
        header[1] = 126;
        uint16_t n_len = htons(len);
        memcpy(&header[2], &n_len, 2);
        header_len = 4;
    } else {
        header[1] = 127;
        uint64_t n_len = htobe64(len);
        memcpy(&header[2], &n_len, 8);
        header_len = 10;
    }

    if (robust_send(client_sock, (char*)header, header_len) == -1) return -1;
    if (robust_send(client_sock, message, len) == -1) return -1;

    return 0;
}
