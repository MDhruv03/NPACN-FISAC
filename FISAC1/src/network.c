/*
    network.c - Robust network I/O for WinSock2.

    Handles partial transmissions, connection resets, and non-blocking
    socket behavior. These functions are the foundation for reliable
    data transfer over TCP in the WebSocket server.
*/

#include "network.h"
#include <stdio.h>

/*
 * robust_send: Guarantees that all `len` bytes are sent over the socket.
 *
 * TCP is a stream protocol - a single send() call may transmit fewer bytes
 * than requested (a "partial send"). This occurs when:
 *   - The kernel send buffer is full (congestion / slow receiver)
 *   - The socket is non-blocking and would need to wait
 *
 * This function loops until all bytes are sent or an unrecoverable error occurs.
 *
 * Returns: total bytes sent on success, -1 on error (connection lost)
 */
int robust_send(SOCKET sockfd, const char *buf, int len) {
    int total = 0;
    while (total < len) {
        int n = send(sockfd, buf + total, len - total, 0);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAECONNRESET || err == WSAECONNABORTED) {
                /* Connection was forcibly closed by remote host */
                return -1;
            }
            if (err == WSAEWOULDBLOCK) {
                /* Non-blocking socket: send buffer is full.
                 * In production, we would use select() to wait for writability.
                 * For simplicity, we yield briefly and retry. */
                Sleep(1);
                continue;
            }
            fprintf(stderr, "[NET] send() error: %d\n", err);
            return -1;
        }
        total += n;
    }
    return total;
}

/*
 * robust_recv: Single recv() call with proper WinSock error handling.
 *
 * Unlike robust_send, this does NOT loop to fill the buffer, because
 * the caller (WebSocket frame parser) needs to control how many bytes
 * to read based on frame headers.
 *
 * Returns:
 *    > 0:  number of bytes received
 *      0:  connection gracefully closed (peer sent FIN)
 *     -1:  WSAEWOULDBLOCK (no data available on non-blocking socket)
 *     -2:  unrecoverable error
 */
int robust_recv(SOCKET sockfd, char *buf, int len) {
    int n = recv(sockfd, buf, len, 0);
    if (n == 0) {
        /* Graceful close: peer sent FIN */
        return 0;
    }
    if (n == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAECONNRESET || err == WSAECONNABORTED) {
            /* Connection was forcibly closed */
            return 0;
        }
        if (err == WSAEWOULDBLOCK) {
            /* No data available right now (non-blocking socket) */
            return -1;
        }
        fprintf(stderr, "[NET] recv() error: %d\n", err);
        return -2;
    }
    return n;
}
