#ifndef NETWORK_H
#define NETWORK_H

#include <sys/types.h>

ssize_t robust_send(int sockfd, const char *buf, size_t len);
ssize_t robust_recv(int sockfd, char *buf, size_t len);

#endif // NETWORK_H
