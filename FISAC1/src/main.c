#include "server.h"

int main() {
    Server server;
    server_init(&server, "127.0.0.1", 8080);
    server_run(&server);
    return 0;
}
