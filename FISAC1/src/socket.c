/*
    socket.c - WinSock2 socket creation and configuration.

    Implements TCP socket creation with advanced socket options:
    - SO_REUSEADDR:  Allows reuse of local addresses (prevents bind failures after restart)
    - TCP_NODELAY:   Disables Nagle's algorithm (reduces latency for small frequent messages)
    - SO_KEEPALIVE:  Enables keep-alive probes (detects dead connections)
    - SO_RCVBUF:     Sets receive buffer size (handles burst traffic under congestion)
*/

#include "socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

/* ---- WinSock Lifecycle ---- */

int winsock_init(void) {
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        fprintf(stderr, "[FATAL] WSAStartup failed with error: %d\n", result);
        return -1;
    }
    printf("[INIT] WinSock2 initialized (version %d.%d)\n",
           LOBYTE(wsa_data.wVersion), HIBYTE(wsa_data.wVersion));
    return 0;
}

void winsock_cleanup(void) {
    WSACleanup();
    printf("[CLEANUP] WinSock2 cleaned up.\n");
}

/* ---- Socket Creation ---- */

SOCKET create_socket(void) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "[FATAL] socket() failed with error: %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    return sock;
}

/* ---- Socket Options Configuration ---- */

void set_socket_options(SOCKET sock) {
    int opt_val;
    int opt_len;
    const char *profile = getenv("FISAC_SOCKOPTS_PROFILE");
    int tuned = 1;

    if (profile && _stricmp(profile, "baseline") == 0) {
        tuned = 0;
    }

    printf("[SOCKOPT] Profile: %s\n", tuned ? "tuned" : "baseline");

    /*
     * SO_REUSEADDR: Allows the server socket to bind to an address that is in
     * a TIME_WAIT state. This is critical for server applications because without it,
     * restarting the server after a crash would fail with "Address already in use"
     * until all TIME_WAIT sockets expire (typically 2*MSL = 60-120 seconds).
     *
     * Impact on our system:
     * - Enables instant server restart during development and after crashes
     * - Prevents bind() failures when rapidly cycling the server
     */
    opt_val = tuned ? 1 : 0;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt_val, sizeof(opt_val)) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] setsockopt(SO_REUSEADDR) failed: %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    printf("[SOCKOPT] SO_REUSEADDR = %d\n", opt_val);

    /*
     * TCP_NODELAY: Disables Nagle's algorithm. Nagle's algorithm batches small
     * outgoing TCP segments and waits for ACKs before sending, which reduces
     * network overhead but introduces latency.
     *
     * Impact on our system:
     * - Location updates are small JSON messages (~100 bytes)
     * - Without TCP_NODELAY, Nagle would buffer these, adding 40-200ms delay
     * - With TCP_NODELAY, each location update is sent immediately
     * - Critical for real-time location broadcasting where latency matters
     */
    opt_val = tuned ? 1 : 0;
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&opt_val, sizeof(opt_val)) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] setsockopt(TCP_NODELAY) failed: %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    printf("[SOCKOPT] TCP_NODELAY = %d\n", opt_val);

    /*
     * SO_KEEPALIVE: Enables TCP keep-alive probes. When a connection is idle
     * for a period (default ~2 hours on Windows), the OS sends probe packets.
     * If the remote host doesn't respond after several probes, the connection
     * is considered dead and closed by the OS.
     *
     * Impact on our system:
     * - Mobile clients may lose network without sending a TCP FIN
     * - Without SO_KEEPALIVE, these "half-open" connections waste server resources
     * - With SO_KEEPALIVE, the OS detects dead connections and notifies select()
     * - Prevents CLOSE_WAIT socket leaks and file descriptor exhaustion
     */
    opt_val = tuned ? 1 : 0;
    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char *)&opt_val, sizeof(opt_val)) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] setsockopt(SO_KEEPALIVE) failed: %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    printf("[SOCKOPT] SO_KEEPALIVE = %d\n", opt_val);

    /*
     * SO_RCVBUF: Sets the size of the kernel receive buffer for this socket.
     * The default on Windows is typically 8KB. We increase it to 128KB.
     *
     * Impact on our system:
     * - During load tests with 200+ clients sending updates every 100ms,
     *   the default buffer can overflow, causing TCP to advertise a zero window
     * - A larger buffer absorbs traffic bursts without dropping data
     * - Improves throughput at the cost of slightly more memory per socket
     * - Reduces retransmissions under high update frequency
     */
    opt_val = tuned ? 131072 : 8192; /* tuned=128KB, baseline=8KB */
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *)&opt_val, sizeof(opt_val)) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] setsockopt(SO_RCVBUF) failed: %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    /* Verify the actual buffer size (OS may adjust) */
    opt_len = sizeof(opt_val);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&opt_val, &opt_len);
    printf("[SOCKOPT] SO_RCVBUF = %d bytes (receive buffer)\n", opt_val);
}

/* ---- Bind and Listen ---- */

void bind_socket(SOCKET sock, const char *ip, int port) {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((u_short)port);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        /* Fallback: bind to all interfaces */
        server_addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "[FATAL] bind() failed with error: %d\n", WSAGetLastError());
        closesocket(sock);
        exit(EXIT_FAILURE);
    }
}

void listen_on_socket(SOCKET sock) {
    if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "[FATAL] listen() failed with error: %d\n", WSAGetLastError());
        closesocket(sock);
        exit(EXIT_FAILURE);
    }
}

/* ---- Non-Blocking Mode ---- */

void set_non_blocking(SOCKET sock) {
    u_long mode = 1;  /* 1 = non-blocking */
    if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] ioctlsocket(FIONBIO) failed: %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
}
