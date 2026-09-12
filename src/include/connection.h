#ifndef CONNECTION_H
#define CONNECTION_H

#include <sys/types.h>

typedef enum {
    CONN_CLIENT_REQUEST,
    CONN_CONNECTING,
    CONN_RELAY,
    CONN_DRAIN_CLIENT
} ConnState;

typedef struct {
    char current_buffer[2048];
    ssize_t read_bytes_size;
    ssize_t offset_bytes;
} PendingWriteData;

typedef struct {
    int connection_fd;
    int upstream_fd;
    ConnState current_state;
    PendingWriteData pending_write_data;
    PendingWriteData pending_upstream_write;
    char method[16];
    char path[256];
    char host[256];
} Connection;

int set_nonblock(int fd);
void close_fd(int fd_index, Connection client_array[], int *client_count);

#endif
