#ifndef NETWORK_H
#define NETWORK_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>

/* Robust send: loops until all bytes are sent. Returns total bytes sent, or -1 on error. */
int robust_send(SOCKET sockfd, const char *buf, int len);

/* Robust recv: single recv call with WinSock error handling. Returns bytes received, 0 on close, -1 on WOULDBLOCK, -2 on error. */
int robust_recv(SOCKET sockfd, char *buf, int len);

#endif /* NETWORK_H */
