#ifndef SERVER_H
#define SERVER_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>
#include "network.h"

#define MAX_CLIENTS 30
#define MAX_USERNAME 64

/* Per-client state tracking */
typedef struct {
    SOCKET sock;           /* Client socket (INVALID_SOCKET if slot empty) */
    int    authenticated;  /* 1 if authenticated, 0 otherwise */
    int    user_id;        /* Database user ID (set after auth) */
    char   username[MAX_USERNAME]; /* Username (set after auth) */
} ClientInfo;

/* Server state */
typedef struct {
    SOCKET     sock;                  /* Listening socket */
    int        running;               /* Server running flag */
    ClientInfo clients[MAX_CLIENTS];  /* Connected client slots */
} Server;

void server_init(Server *server, const char *ip, int port);
void server_run(Server *server);
void server_broadcast(Server *server, const char *message, SOCKET sender_sock);
void server_shutdown(Server *server);

#endif /* SERVER_H */
