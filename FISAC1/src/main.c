/*
    main.c - Entry point for the Real-Time Location Sharing Server.

    Initializes WinSock2 and starts the WebSocket server on port 8080.
    Database operations are delegated to the Python Flask backend.

    Architecture:
    [Web Browser] ←→ [WebSocket over TCP] ←→ [C Server] ←(HTTP)→ [Python Flask DB]
*/

#include "server.h"
#include "socket.h"
#include "protocol.h"
#include "http_client.h"
#include <stdio.h>
#include <signal.h>
#include <windows.h>

static Server g_server;
static volatile int g_running = 1;

/*
 * Signal handler for graceful shutdown (Ctrl+C).
 * Sets the server running flag to 0, allowing the select() loop
 * to exit cleanly and perform proper socket cleanup.
 */
static void signal_handler(int sig) {
    (void)sig;
    printf("\n[SIGNAL] Shutdown signal received. Cleaning up...\n");
    g_server.running = 0;
    g_running = 0;
}

int main(void) {
    printf("=============================================\n");
    printf("  Real-Time Location Sharing Server (FISAC)  \n");
    printf("  WinSock2 Server + Python Flask Backend     \n");
    printf("=============================================\n\n");

    /* Register signal handler for Ctrl+C */
    signal(SIGINT, signal_handler);

    /* Step 1: Initialize WinSock2 */
    if (winsock_init() != 0) {
        fprintf(stderr, "[FATAL] Cannot initialize WinSock2. Exiting.\n");
        return 1;
    }

    /* Wait a moment for Python service to start if launched via run_all.bat */
    printf("[MAIN] Waiting 2 seconds for Python DB Service to start...\n");
    Sleep(2000);

    /* Step 2: Initialize and run the server */
    server_init(&g_server, "0.0.0.0", 8080);
    server_run(&g_server);

    /* Step 3: Cleanup */
    winsock_cleanup();

    printf("[MAIN] Server exited cleanly.\n");
    return 0;
}
