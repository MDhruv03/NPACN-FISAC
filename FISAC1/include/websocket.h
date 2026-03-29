#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>
#include <stdint.h>

int websocket_handshake(SOCKET client_sock);
int websocket_frame_recv(SOCKET client_sock, char *buffer, int buffer_size);
int websocket_frame_send(SOCKET client_sock, const char *message, uint64_t len, int type);

#endif /* WEBSOCKET_H */
