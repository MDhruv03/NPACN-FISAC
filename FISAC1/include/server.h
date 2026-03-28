#ifndef SERVER_H
#define SERVER_H

#include <netinet/in.h>
#include "network.h"

#define MAX_CLIENTS 30

typedef struct {
    int sock;
    struct sockaddr_in address;
    int addr_len;
    int running;
    int clients[MAX_CLIENTS];
} Server;

void server_init(Server *server, const char *ip, int port);
void server_run(Server *server);
void server_broadcast(Server *server, const char *message, int sender_sock);
void server_shutdown(Server *server);

#endif // SERVER_H
