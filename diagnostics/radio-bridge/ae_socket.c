#include "ae_socket.h"
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>

#define AE_MAX_FRAME (1024u * 1024u)

static int read_all(int fd, void *data, size_t length) {
    uint8_t *p = data;
    while (length) {
        ssize_t n = read(fd, p, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += (size_t)n;
        length -= (size_t)n;
    }
    return 0;
}

static int send_all(int fd, const void *data, size_t length) {
    const uint8_t *p = data;
    while (length) {
        ssize_t n = send(fd, p, length, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += (size_t)n;
        length -= (size_t)n;
    }
    return 0;
}

int ae_write_frame(int fd, const void *payload, size_t length) {
    if (!payload || length == 0 || length > AE_MAX_FRAME) return -1;
    uint8_t header[4] = {(uint8_t)(length >> 24), (uint8_t)(length >> 16),
                         (uint8_t)(length >> 8), (uint8_t)length};
    if (send_all(fd, header, sizeof header) < 0) return -1;
    return send_all(fd, payload, length);
}

int ae_read_frame(int fd, void *buffer, size_t capacity, size_t *length) {
    uint8_t header[4];
    if (!buffer || !length || read_all(fd, header, sizeof header) < 0) return -1;
    size_t n = ((size_t)header[0] << 24) | ((size_t)header[1] << 16) |
               ((size_t)header[2] << 8) | header[3];
    if (n == 0 || n > AE_MAX_FRAME || n > capacity) return -1;
    if (read_all(fd, buffer, n) < 0) return -1;
    *length = n;
    return 0;
}
