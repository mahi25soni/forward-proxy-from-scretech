#ifndef PROXY_H
#define PROXY_H

#include <connection.h>

int stash_pending_write(Connection *y, char *proxy_read_buf, ssize_t read_size, ssize_t offset_bytes);
int stash_pending_upstream_write(Connection *y, char *proxy_read_buf, ssize_t read_size, ssize_t offset_bytes);
int drain_write_buffer(int fd_index, Connection client_array[], int *client_count);
int drain_upstream_write(int fd_index, Connection client_array[], int *client_count);
int finish_connect(int fd_index, Connection client_array[], int *client_count);
int handle_upstream(int fd_index, Connection client_array[], int *client_count);
int handle_client_relay(int fd_index, Connection client_array[], int *client_count);
int handle_client(int fd_index, Connection client_array[], int *client_count);

#endif
