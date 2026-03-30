/*
    server.c - Concurrent WebSocket server using WinSock2 and select().

    This server implements an event-driven concurrency model using the select()
    system call for I/O multiplexing. This is a single-threaded model where:

    1. A single thread monitors all connected sockets for activity
    2. select() blocks until at least one socket has data to read
    3. The server then processes each ready socket sequentially

    Justification for select() over threads:
    - No thread synchronization needed (no mutexes, no race conditions)
    - Lower memory overhead (no per-thread stacks)
    - Simpler code with fewer concurrency bugs
    - Sufficient for I/O-bound workloads (our server mostly relays data)
    - Data integrity: shared client list is safely accessed without locks

    Trade-off: Cannot leverage multiple CPU cores. A blocking operation
    (like a slow database write) blocks all clients. We mitigate this
    by using SQLite WAL mode for non-blocking reads.
*/

#include "server.h"
#include "socket.h"
#include "network.h"
#include "websocket.h"
#include "protocol.h"
#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ws2tcpip.h>

/*
 * server_init: Initialize the server state.
 *
 * Creates the listening socket, configures socket options,
 * binds to the specified address, and initializes client slots.
 */
void server_init(Server *server, const char *ip, int port) {
    server->sock = create_socket();
    set_socket_options(server->sock);
    bind_socket(server->sock, ip, port);
    set_non_blocking(server->sock);
    server->running = 1;

    /* Initialize all client slots to empty */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        server->clients[i].sock = INVALID_SOCKET;
        server->clients[i].authenticated = 0;
        server->clients[i].user_id = 0;
        server->clients[i].username[0] = '\0';
    }
}

/*
 * server_run: Main event loop using select() for I/O multiplexing.
 *
 * The select() call monitors:
 * - The listening socket for new incoming connections
 * - All connected client sockets for incoming WebSocket frames
 *
 * On Windows, select()'s first parameter (nfds) is ignored,
 * but we compute it for POSIX compatibility documentation.
 *
 * Flow per iteration:
 * 1. Build fd_set of all active sockets
 * 2. select() blocks until activity on any socket
 * 3. If listening socket is ready → accept new connection + handshake
 * 4. For each client socket that's ready → read frame, process, broadcast
 * 5. Handle disconnections (recv returns 0)
 */
void server_run(Server *server) {
    listen_on_socket(server->sock);
    printf("\n");
    printf("========================================\n");
    printf("   Location Server listening on :8080   \n");
    printf("========================================\n");
    printf("  Open frontend/index.html in browser   \n");
    printf("  Test users: user1/pass1, user2/pass2  \n");
    printf("========================================\n\n");
    log_event("INFO", "Server started on port 8080");

    fd_set readfds;
    struct timeval timeout;

    while (server->running) {
        FD_ZERO(&readfds);
        FD_SET(server->sock, &readfds);

        /* Add all active client sockets to the read set */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (server->clients[i].sock != INVALID_SOCKET) {
                FD_SET(server->clients[i].sock, &readfds);
            }
        }

        /* Timeout for select() - allows periodic server maintenance */
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        /*
         * select() on Windows: the first parameter (nfds) is ignored.
         * On POSIX it would be max_fd + 1. We pass 0 for clarity.
         */
        int activity = select(0, &readfds, NULL, NULL, &timeout);

        if (activity == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEINTR) {
                fprintf(stderr, "[SERVER] select() error: %d\n", err);
                log_event("ERROR", "select() error");
            }
            continue;
        }

        if (activity == 0) {
            /* Timeout - no activity. Could do periodic cleanup here. */
            continue;
        }

        /* ---- Handle new incoming connections ---- */
        if (FD_ISSET(server->sock, &readfds)) {
            struct sockaddr_in client_addr;
            int addrlen = sizeof(client_addr);
            SOCKET new_socket = accept(server->sock, (struct sockaddr *)&client_addr, &addrlen);

            if (new_socket == INVALID_SOCKET) {
                int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK) {
                    fprintf(stderr, "[SERVER] accept() error: %d\n", err);
                }
            } else {
                /* Explicitly set the client socket to BLOCKING mode. 
                   Since the listen socket is non-blocking, accepted sockets inherit this on Windows.
                   Our framing logic requires blocking recv() to reliably assemble fragmented TCP frames. */
                unsigned long iMode = 0;
                ioctlsocket(new_socket, FIONBIO, &iMode);

                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                int client_port = ntohs(client_addr.sin_port);

                printf("[CONN] New connection from %s:%d (fd=%lld)\n",
                       client_ip, client_port, (long long)new_socket);

                char log_msg[256];
                snprintf(log_msg, sizeof(log_msg), "New connection from %s:%d", client_ip, client_port);
                log_event("INFO", log_msg);

                /* Perform WebSocket handshake */
                if (websocket_handshake(new_socket) == 0) {
                    printf("[CONN] WebSocket handshake OK\n");
                    log_event("INFO", "WebSocket handshake successful");

                    /* Find an empty client slot */
                    int added = 0;
                    for (int i = 0; i < MAX_CLIENTS; i++) {
                        if (server->clients[i].sock == INVALID_SOCKET) {
                            server->clients[i].sock = new_socket;
                            server->clients[i].authenticated = 0;
                            server->clients[i].user_id = 0;
                            server->clients[i].username[0] = '\0';
                            printf("[CONN] Client added to slot %d\n", i);
                            added = 1;
                            break;
                        }
                    }
                    if (!added) {
                        fprintf(stderr, "[CONN] Max clients reached, rejecting connection\n");
                        log_event("WARN", "Max clients reached, connection rejected");
                        closesocket(new_socket);
                    }
                } else {
                    printf("[CONN] WebSocket handshake FAILED\n");
                    log_event("WARN", "WebSocket handshake failed");
                    closesocket(new_socket);
                }
            }
        }

        /* ---- Handle data from connected clients ---- */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            SOCKET sd = server->clients[i].sock;
            if (sd == INVALID_SOCKET) continue;

            if (FD_ISSET(sd, &readfds)) {
                char buffer[4096];
                int len = websocket_frame_recv(sd, buffer, sizeof(buffer));

                if (len > 0) {
                    /* Process the message (auth, location, etc.) */
                    handle_message(&server->clients[i], buffer);

                    /* Broadcast location messages to all other authenticated clients */
                    cJSON *json = cJSON_Parse(buffer);
                    if (json) {
                        cJSON *type = cJSON_GetObjectItem(json, "type");
                        if (type && type->type == cJSON_String && strcmp(type->valuestring, MSG_TYPE_LOCATION) == 0) {
                            /* Only broadcast if sender is authenticated */
                            if (server->clients[i].authenticated) {
                                server_broadcast(server, buffer, sd);
                            }
                        }
                        cJSON_Delete(json);
                    }
                } else if (len == 0) {
                    /* Control frame handled internally (ping/pong/unsupported). */
                    continue;
                } else {
                    /*
                     * Client disconnected (recv returned 0 or error).
                     *
                     * TCP state transition:
                     * - If client sent FIN → connection moves to CLOSE_WAIT on server side
                     * - closesocket() sends our FIN → moves to LAST_ACK → then closed
                     * - The client side will enter TIME_WAIT (2*MSL ~ 60s)
                     *
                     * If client crashed without FIN:
                     * - SO_KEEPALIVE probes will eventually detect the dead connection
                     * - Until then, select() won't flag this socket as readable
                     */
                    char log_msg[256];
                    if (server->clients[i].authenticated) {
                        printf("[DISC] User '%s' disconnected (slot %d)\n",
                               server->clients[i].username, i);
                        snprintf(log_msg, sizeof(log_msg), "User disconnected: %s",
                                 server->clients[i].username);
                    } else {
                        printf("[DISC] Unauthenticated client disconnected (slot %d)\n", i);
                        snprintf(log_msg, sizeof(log_msg), "Unauthenticated client disconnected");
                    }
                    log_event("INFO", log_msg);

                    closesocket(sd);
                    server->clients[i].sock = INVALID_SOCKET;
                    server->clients[i].authenticated = 0;
                    server->clients[i].user_id = 0;
                    server->clients[i].username[0] = '\0';
                }
            }
        }
    }

    server_shutdown(server);
}

/*
 * server_broadcast: Send a message to all connected, authenticated clients
 * except the sender.
 *
 * This is the core of the real-time location sharing: when one client
 * sends a location update, all other clients receive it instantly.
 */
void server_broadcast(Server *server, const char *message, SOCKET sender_sock) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        SOCKET sd = server->clients[i].sock;
        if (sd != INVALID_SOCKET && sd != sender_sock && server->clients[i].authenticated) {
            if (websocket_frame_send(sd, message, (uint64_t)strlen(message), 1) == -1) {
                /* Send failed - client probably disconnected */
                printf("[BCAST] Send failed to slot %d, will be cleaned up\n", i);
            }
        }
    }
}

/*
 * server_shutdown: Graceful server shutdown.
 *
 * Closes all client connections and the listening socket.
 * Each closesocket() triggers the TCP four-way handshake:
 *   Server → FIN → Client
 *   Client → ACK → Server
 *   Client → FIN → Server
 *   Server → ACK → Client
 */
void server_shutdown(Server *server) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i].sock != INVALID_SOCKET) {
            closesocket(server->clients[i].sock);
            server->clients[i].sock = INVALID_SOCKET;
        }
    }
    closesocket(server->sock);
    printf("[SERVER] Server shut down.\n");
    log_event("INFO", "Server shut down");
}
