#include "network.h"
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

ssize_t robust_send(int sockfd, const char *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(sockfd, buf + total, len - total, 0);
        if (n == -1) {
            if (errno == ECONNRESET || errno == EPIPE) {
                return -1; // Connection closed
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Temporary error, try again later
                continue;
            }
            return -1; // Other error
        }
        total += n;
    }
    return total;
}

ssize_t robust_recv(int sockfd, char *buf, size_t len) {
    ssize_t n = recv(sockfd, buf, len, 0);
    if (n == -1) {
        if (errno == ECONNRESET || errno == EPIPE) {
            return 0; // Connection closed
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1; // No data available right now
        }
        return -2; // Other error
    }
    return n;
}
