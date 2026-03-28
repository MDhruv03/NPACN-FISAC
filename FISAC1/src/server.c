#include "server.h"
#include "socket.h"
#include "network.h"
#include "websocket.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

void server_init(Server *server, const char *ip, int port) {
    server->sock = create_socket();
    set_socket_options(server->sock);
    bind_socket(server->sock, ip, port);
    set_non_blocking(server->sock);
    server->running = 1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        server->clients[i] = 0;
    }
}

void server_run(Server *server) {
    listen_on_socket(server->sock);
    printf("Server listening on port 8080\n");
    log_event("INFO", "Server started");

    fd_set readfds;
    int max_sd, activity;

    while (server->running) {
        FD_ZERO(&readfds);
        FD_SET(server->sock, &readfds);
        max_sd = server->sock;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = server->clients[i];
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }
            if (sd > max_sd) {
                max_sd = sd;
            }
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
            log_event("ERROR", "Select error");
        }

        if (FD_ISSET(server->sock, &readfds)) {
            int new_socket;
            struct sockaddr_in client_addr;
            int addrlen = sizeof(client_addr);
            if ((new_socket = accept(server->sock, (struct sockaddr *)&client_addr, (socklen_t*)&addrlen))<0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
            set_non_blocking(new_socket);

            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            int client_port = ntohs(client_addr.sin_port);

            printf("New connection, socket fd is %d, ip is : %s, port : %d\n", new_socket, client_ip, client_port);
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "New connection from %s:%d", client_ip, client_port);
            log_event("INFO", log_msg);


            if (websocket_handshake(new_socket) == 0) {
                printf("WebSocket handshake successful\n");
                log_event("INFO", "WebSocket handshake successful");
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (server->clients[i] == 0) {
                        server->clients[i] = new_socket;
                        printf("Adding to list of sockets as %d\n", i);
                        break;
                    }
                }
            } else {
                printf("WebSocket handshake failed\n");
                log_event("WARN", "WebSocket handshake failed");
                close(new_socket);
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = server->clients[i];
            if (FD_ISSET(sd, &readfds)) {
                char buffer[2048];
                int len = websocket_frame_recv(sd, buffer, sizeof(buffer));
                if (len > 0) {
                    handle_message(sd, buffer);
                    server_broadcast(server, buffer, sd);
                } else {
                    struct sockaddr_in client_addr;
                    int addrlen = sizeof(client_addr);
                    getpeername(sd, (struct sockaddr*)&client_addr, (socklen_t*)&addrlen);
                    char client_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                    int client_port = ntohs(client_addr.sin_port);

                    printf("Host disconnected, ip %s, port %d\n", client_ip, client_port);
                    char log_msg[256];
                    snprintf(log_msg, sizeof(log_msg), "Host disconnected %s:%d", client_ip, client_port);
                    log_event("INFO", log_msg);

                    close(sd);
                    server->clients[i] = 0;
                }
            }
        }
    }
    server_shutdown(server);
}

void server_broadcast(Server *server, const char *message, int sender_sock) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        int sd = server->clients[i];
        if (sd > 0 && sd != sender_sock) {
            websocket_frame_send(sd, message, strlen(message), 1); // 1 for text frame
        }
    }
}

void server_shutdown(Server *server) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i] > 0) {
            close(server->clients[i]);
        }
    }
    close(server->sock);
    printf("Server shut down.\n");
    log_event("INFO", "Server shut down");
}
