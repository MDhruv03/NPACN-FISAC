#ifndef SOCKET_H
#define SOCKET_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>

/* Initialize WinSock2 (must be called before any socket operations) */
int winsock_init(void);

/* Cleanup WinSock2 (call at program exit) */
void winsock_cleanup(void);

/* Create a TCP socket */
SOCKET create_socket(void);

/* Configure socket options: SO_REUSEADDR, TCP_NODELAY, SO_KEEPALIVE, SO_RCVBUF */
void set_socket_options(SOCKET sock);

/* Bind socket to IP and port */
void bind_socket(SOCKET sock, const char *ip, int port);

/* Start listening on socket */
void listen_on_socket(SOCKET sock);

/* Set socket to non-blocking mode */
void set_non_blocking(SOCKET sock);

#endif /* SOCKET_H */
