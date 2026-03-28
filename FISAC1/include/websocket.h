#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdint.h>

int websocket_handshake(int client_sock);
int websocket_frame_recv(int client_sock, char *buffer, int buffer_size);
int websocket_frame_send(int client_sock, const char *message, uint64_t len, int type);

#endif // WEBSOCKET_H
