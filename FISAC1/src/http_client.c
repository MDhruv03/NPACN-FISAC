/*
    http_client.c - Simple, blocking WinSock2 HTTP client.

    Creates a new TCP socket, connects to localhost:5000 (the Flask server),
    sends an HTTP POST request containing a JSON payload, waits for the response,
    extracts the response body, and returns it.

    In a high-performance system, we would use non-blocking I/O or connection pooling
    to avoid stalling the main select() loop. For simplicity and robustness in this
    academic project, we use blocking connections for database operations.
*/

#include "http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ws2tcpip.h>

int http_post_json(const char *endpoint, const char *json_payload, char *response_buffer, int buffer_size) {
    if (response_buffer) {
        response_buffer[0] = '\0';
    }

    SOCKET client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_sock == INVALID_SOCKET) {
        return -1;
    }

    /* Set short timeouts to prevent blocking the C server forever if Python dies */
    int timeout = 2000; /* 2 seconds */
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5000); /* Port 5000 = Flask default */
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(client_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(client_sock);
        return -1;
    }

    int payload_len = (int)strlen(json_payload);
    char request[4096];
    int req_len = snprintf(request, sizeof(request),
        "POST %s HTTP/1.0\r\n"
        "Host: 127.0.0.1:5000\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "%s",
        endpoint, payload_len, json_payload);

    /* Send request */
    int total_sent = 0;
    while (total_sent < req_len) {
        int bytes = send(client_sock, request + total_sent, req_len - total_sent, 0);
        if (bytes <= 0) {
            closesocket(client_sock);
            return -1;
        }
        total_sent += bytes;
    }

    /* Read response */
    char recv_buf[8192];
    int total_recv = 0;
    while (total_recv < (int)sizeof(recv_buf) - 1) {
        int bytes = recv(client_sock, recv_buf + total_recv, sizeof(recv_buf) - 1 - total_recv, 0);
        if (bytes <= 0) break;
        total_recv += bytes;
    }
    recv_buf[total_recv] = '\0';

    closesocket(client_sock);

    /* Find the HTTP body (separated by \r\n\r\n) */
    char *body = strstr(recv_buf, "\r\n\r\n");
    if (body) {
        body += 4; /* skip the \r\n\r\n */
        if (response_buffer && buffer_size > 0) {
            strncpy(response_buffer, body, buffer_size - 1);
            response_buffer[buffer_size - 1] = '\0';
        }
        return 0;
    }

    return -1;
}
