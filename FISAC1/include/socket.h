#ifndef SOCKET_H
#define SOCKET_H

int create_socket();
void set_socket_options(int sock);
void bind_socket(int sock, const char *ip, int port);
void listen_on_socket(int sock);
void set_non_blocking(int sock);

#endif // SOCKET_H
