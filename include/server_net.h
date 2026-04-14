#ifndef SERVER_NET_H
#define SERVER_NET_H

#include <stddef.h>

int send_linef(int fd, const char *fmt, ...);
int recv_line(int fd, char *buffer, size_t size);

#endif
