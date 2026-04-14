#include "common.h"
#include "server_net.h"

#include <errno.h>
#include <stdarg.h>
#include <sys/socket.h>

#define SERVER_IO_MAX_LINE 2048

static int send_all(int fd, const char *buffer, size_t length) {
    size_t sent = 0;

    while (sent < length) {
        ssize_t written = send(fd, buffer + sent, length - sent, 0);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (written == 0) {
            return 0;
        }
        sent += (size_t)written;
    }

    return 1;
}

int send_linef(int fd, const char *fmt, ...) {
    char buffer[SERVER_IO_MAX_LINE];
    size_t len;
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    len = strlen(buffer);
    if (len == 0 || buffer[len - 1] != '\n') {
        if (len + 1 >= sizeof(buffer)) {
            return 0;
        }
        buffer[len] = '\n';
        buffer[len + 1] = '\0';
        len++;
    }

    return send_all(fd, buffer, len);
}

int recv_line(int fd, char *buffer, size_t size) {
    size_t idx = 0;

    while (idx < size - 1) {
        char ch;
        ssize_t result = recv(fd, &ch, 1, 0);

        if (result == 0) {
            if (idx == 0) {
                return 0;
            }
            break;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            break;
        }

        buffer[idx++] = ch;
    }

    buffer[idx] = '\0';
    return 1;
}
