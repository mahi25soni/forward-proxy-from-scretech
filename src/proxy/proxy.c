#include <proxy.h>
#include <handler.h>
#include <parser.h>
#include <error.h>
#include <connection.h>

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int stash_pending_write(Connection *y, char *proxy_read_buf, ssize_t read_size, ssize_t offset_bytes) {
    if (read_size > (ssize_t)sizeof(y->pending_write_data.current_buffer)) {
        return -1;
    }
    memcpy(y->pending_write_data.current_buffer, proxy_read_buf, (size_t)read_size);
    y->pending_write_data.read_bytes_size = read_size;
    y->pending_write_data.offset_bytes = offset_bytes;
    y->current_state = CONN_DRAIN_CLIENT;
    return 0;
}

int stash_pending_upstream_write(Connection *y, char *proxy_read_buf, ssize_t read_size, ssize_t offset_bytes) {
    if (read_size > (ssize_t)sizeof(y->pending_upstream_write.current_buffer)) {
        return -1;
    }
    memcpy(y->pending_upstream_write.current_buffer, proxy_read_buf, (size_t)read_size);
    y->pending_upstream_write.read_bytes_size = read_size;
    y->pending_upstream_write.offset_bytes = offset_bytes;
    return 0;
}

static int send_connect_established(Connection *y) {
    static const char msg[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
    ssize_t n = (ssize_t)(sizeof(msg) - 1);
    ssize_t written = write(y->connection_fd, msg, (size_t)n);

    if (written == n) {
        y->current_state = CONN_RELAY;
        return 0;
    }
    if (written < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return stash_pending_write(y, (char *)msg, n, 0);
        }
        perror("connect established");
        return -1;
    }
    return stash_pending_write(y, (char *)msg, n, written);
}

int drain_write_buffer(int fd_index, Connection client_array[], int *client_count) {
    Connection *y = &client_array[fd_index];
    ssize_t remaining = y->pending_write_data.read_bytes_size - y->pending_write_data.offset_bytes;
    if (remaining <= 0) {
        y->current_state = CONN_RELAY;
        return 1;
    }

    ssize_t write_size = write(
        y->connection_fd,
        y->pending_write_data.current_buffer + y->pending_write_data.offset_bytes,
        (size_t)remaining
    );

    if (write_size == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        }
        perror("drain write");
        close_fd(fd_index, client_array, client_count);
        return 0;
    }

    y->pending_write_data.offset_bytes += write_size;
    if (y->pending_write_data.offset_bytes >= y->pending_write_data.read_bytes_size) {
        y->pending_write_data.read_bytes_size = 0;
        y->pending_write_data.offset_bytes = 0;
        y->current_state = CONN_RELAY;
    }
    return 1;
}

int drain_upstream_write(int fd_index, Connection client_array[], int *client_count) {
    Connection *y = &client_array[fd_index];
    ssize_t remaining = y->pending_upstream_write.read_bytes_size - y->pending_upstream_write.offset_bytes;
    if (remaining <= 0) {
        y->pending_upstream_write.read_bytes_size = 0;
        y->pending_upstream_write.offset_bytes = 0;
        return 1;
    }

    ssize_t write_size = write(
        y->upstream_fd,
        y->pending_upstream_write.current_buffer + y->pending_upstream_write.offset_bytes,
        (size_t)remaining
    );

    if (write_size == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        }
        perror("drain upstream write");
        close_fd(fd_index, client_array, client_count);
        return 0;
    }

    y->pending_upstream_write.offset_bytes += write_size;
    if (y->pending_upstream_write.offset_bytes >= y->pending_upstream_write.read_bytes_size) {
        y->pending_upstream_write.read_bytes_size = 0;
        y->pending_upstream_write.offset_bytes = 0;
    }
    return 1;
}

int finish_connect(int fd_index, Connection client_array[], int *client_count) {
    Connection *y = &client_array[fd_index];
    int so_error = 0;
    socklen_t so_len = sizeof(so_error);

    if (getsockopt(y->upstream_fd, SOL_SOCKET, SO_ERROR, &so_error, &so_len) < 0 || so_error != 0) {
        http_error(y->connection_fd, 502, "Could not connect to upstream");
        close_fd(fd_index, client_array, client_count);
        return 0;
    }

    if (strcmp(y->method, "CONNECT") == 0) {
        if (send_connect_established(y) < 0) {
            close_fd(fd_index, client_array, client_count);
            return 0;
        }
        return 1;
    }

    if (create_http_request(y->upstream_fd, y->method, y->path, y->host) < 0) {
        http_error(y->connection_fd, 502, "Could not send request to upstream");
        close_fd(fd_index, client_array, client_count);
        return 0;
    }

    y->current_state = CONN_RELAY;
    return 1;
}

int handle_upstream(int fd_index, Connection client_array[], int *client_count) {
    Connection *y = &client_array[fd_index];
    char proxy_read_buf[2048];
    ssize_t read_size = read(y->upstream_fd, proxy_read_buf, sizeof(proxy_read_buf));

    if (read_size == 0) {
        printf("[DISCONNECT] upstream fd=%d\n", y->upstream_fd);
        close_fd(fd_index, client_array, client_count);
        return 0;
    }

    if (read_size == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        }
        perror("read proxy error");
        close_fd(fd_index, client_array, client_count);
        return 0;
    }

    ssize_t write_size = write(y->connection_fd, proxy_read_buf, (size_t)read_size);
    if (write_size == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (stash_pending_write(y, proxy_read_buf, read_size, 0) < 0) {
                close_fd(fd_index, client_array, client_count);
                return 0;
            }
            return 1;
        }
        perror("write proxy error");
        close_fd(fd_index, client_array, client_count);
        return 0;
    }

    if (write_size < read_size) {
        if (stash_pending_write(y, proxy_read_buf, read_size, write_size) < 0) {
            close_fd(fd_index, client_array, client_count);
            return 0;
        }
        return 1;
    }

    return 1;
}

int handle_client_relay(int fd_index, Connection client_array[], int *client_count) {
    Connection *y = &client_array[fd_index];
    char proxy_read_buf[2048];
    ssize_t read_size = read(y->connection_fd, proxy_read_buf, sizeof(proxy_read_buf));

    if (read_size == 0) {
        printf("[DISCONNECT] client fd=%d\n", y->connection_fd);
        close_fd(fd_index, client_array, client_count);
        return 0;
    }

    if (read_size == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        }
        perror("read client relay error");
        close_fd(fd_index, client_array, client_count);
        return 0;
    }

    ssize_t write_size = write(y->upstream_fd, proxy_read_buf, (size_t)read_size);
    if (write_size == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (stash_pending_upstream_write(y, proxy_read_buf, read_size, 0) < 0) {
                close_fd(fd_index, client_array, client_count);
                return 0;
            }
            return 1;
        }
        perror("write client relay error");
        close_fd(fd_index, client_array, client_count);
        return 0;
    }

    if (write_size < read_size) {
        if (stash_pending_upstream_write(y, proxy_read_buf, read_size, write_size) < 0) {
            close_fd(fd_index, client_array, client_count);
            return 0;
        }
        return 1;
    }

    return 1;
}

int handle_client(int fd_index, Connection client_array[], int *client_count) {
        int fd = client_array[fd_index].connection_fd;
        char buf[2000];
        ssize_t read_bytes = read(fd, buf, sizeof(buf) - 1);
        if (read_bytes == 0) {
            printf("[DISCONNECT] fd=%d\n", fd);
            close_fd(fd_index, client_array, client_count);
            return 0;
        }

        if (read_bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 1;
            } else {
                perror("read");
                close_fd(fd_index, client_array, client_count);
                return 0;
            }
        }
        buf[read_bytes] = '\0';

        ProxyPathData proxy = {0};
        handle_http_request(fd, buf, &proxy);

        if (proxy.host[0] != '\0') {
            char port_str[8];
            snprintf(port_str, sizeof(port_str), "%d", proxy.port);

            strncpy(client_array[fd_index].method, proxy.method, sizeof(client_array[fd_index].method) - 1);
            strncpy(client_array[fd_index].path, proxy.path, sizeof(client_array[fd_index].path) - 1);
            strncpy(client_array[fd_index].host, proxy.host, sizeof(client_array[fd_index].host) - 1);
            client_array[fd_index].method[sizeof(client_array[fd_index].method) - 1] = '\0';
            client_array[fd_index].path[sizeof(client_array[fd_index].path) - 1] = '\0';
            client_array[fd_index].host[sizeof(client_array[fd_index].host) - 1] = '\0';

            struct addrinfo hints, *res, *p;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            int status = getaddrinfo(proxy.host, port_str, &hints, &res);
            if (status != 0) {
                fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
                http_error(fd, 502, "Could not resolve upstream host");
                return 1;
            }

            int upstream_fd = -1;
            int connecting = 0;
            for (p = res; p != NULL; p = p->ai_next) {
                int try_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (try_fd == -1) {
                    perror("upstream socket");
                    continue;
                }

                if (set_nonblock(try_fd) == -1) {
                    perror("FD Flag changing issue");
                    close(try_fd);
                    continue;
                }

                int connect_rc = connect(try_fd, p->ai_addr, p->ai_addrlen);
                if (connect_rc == 0) {
                    upstream_fd = try_fd;
                    connecting = 0;
                    break;
                }
                if (errno == EINPROGRESS) {
                    upstream_fd = try_fd;
                    connecting = 1;
                    break;
                }
                perror("connect");
                close(try_fd);
            }
            freeaddrinfo(res);

            if (upstream_fd == -1) {
                fprintf(stderr, "Could not connect to %s:%s\n", proxy.host, port_str);
                http_error(fd, 502, "Could not connect to upstream");
                return 1;
            }

            client_array[fd_index].upstream_fd = upstream_fd;

            if (connecting) {
                client_array[fd_index].current_state = CONN_CONNECTING;
                printf("[UPSTREAM] connecting fd=%d to %s:%d\n",
                       upstream_fd, proxy.host, proxy.port);
                return 1;
            }

            printf("[UPSTREAM] connected fd=%d to %s:%d\n",
                   upstream_fd, proxy.host, proxy.port);

            if (strcmp(proxy.method, "CONNECT") == 0) {
                if (send_connect_established(&client_array[fd_index]) < 0) {
                    close_fd(fd_index, client_array, client_count);
                    return 0;
                }
                return 1;
            }

            if (create_http_request(upstream_fd, proxy.method, proxy.path, proxy.host) < 0) {
                http_error(fd, 502, "Could not send request to upstream");
                close_fd(fd_index, client_array, client_count);
                return 0;
            }

            client_array[fd_index].current_state = CONN_RELAY;
            return 1;
        }

        return 1;
}
