#include "socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <fcntl.h>

int create_socket() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    return sock;
}

void set_socket_options(int sock) {
    int opt = 1;
    // SO_REUSEADDR: Allows reuse of local addresses
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(EXIT_FAILURE);
    }
    // TCP_NODELAY: Disables Nagle's algorithm, reducing latency
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt))) {
        perror("setsockopt(TCP_NODELAY) failed");
        exit(EXIT_FAILURE);
    }
    // SO_KEEPALIVE: Enables keep-alive messages to detect dead connections
    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt))) {
        perror("setsockopt(SO_KEEPALIVE) failed");
        exit(EXIT_FAILURE);
    }
    // SO_RCVBUF: Sets the size of the receive buffer
    int rcvbuf_size = 131072; // 128KB
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size))) {
        perror("setsockopt(SO_RCVBUF) failed");
        exit(EXIT_FAILURE);
    }
}

void bind_socket(int sock, const char *ip, int port) {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
}

void listen_on_socket(int sock) {
    if (listen(sock, 3) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
}

void set_non_blocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL) failed");
        exit(EXIT_FAILURE);
    }
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL) failed");
        exit(EXIT_FAILURE);
    }
}
