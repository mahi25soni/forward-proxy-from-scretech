#include <connection.h>

#include <fcntl.h>
#include <unistd.h>

int set_nonblock(int fd) {
    int current_flags = fcntl(fd, F_GETFL);
    if (current_flags == -1) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, current_flags | O_NONBLOCK) == -1) {
        return -1;
    }
    return 0;
}

void close_fd(int fd_index, Connection client_array[], int *client_count) {
    Connection fd_pool = client_array[fd_index];
    if (fd_pool.connection_fd != -1) {
        close(fd_pool.connection_fd);
    }
    if (fd_pool.upstream_fd != -1) {
        close(fd_pool.upstream_fd);
    }
    client_array[fd_index] = client_array[*client_count - 1];
    (*client_count) -= 1;
}
