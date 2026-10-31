#ifndef CODEX_AE_SOCKET_H
#define CODEX_AE_SOCKET_H
#include <stddef.h>

/* All operations return 0 on success, -1 on EOF, malformed frame or I/O error.
 * On an error the caller must close the connection; it is no longer framed.
 * The caller must serialize writes to a given fd. */
int ae_write_frame(int fd, const void *payload, size_t length);
int ae_read_frame(int fd, void *buffer, size_t capacity, size_t *length);
#endif
