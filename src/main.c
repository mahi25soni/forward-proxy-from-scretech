#include <connection.h>
#include <proxy.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <errno.h>

volatile sig_atomic_t keep_running = 1;
int PROXY_READ_BUFFER_SIZE = 2048;
fd_set read_fds;
fd_set write_fds;

void handle_interrupt(int signal_num) {
    (void)signal_num;
    keep_running = 0;
}

void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_interrupt;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int main(void) {
    setup_signals();

    int listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_socket == -1) {
        perror("Listing socket");
        exit(1);
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listening_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(listening_socket);
        perror("Socket Binding");
        exit(1);
    }

    if (listen(listening_socket, 200) < 0) {
        close(listening_socket);
        perror("Socket Binding");
        exit(1);
    }

    Connection client_array[1024];
    int client_count = 0;
    memset(&client_array[0], 0, sizeof(Connection));
    client_array[0].connection_fd = listening_socket;
    client_array[0].upstream_fd = -1;
    client_count = 1;

    while (keep_running) {
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        int max_fd = -1;
        for (int i = 0; i < client_count; i++) {
            int upstream_pending = client_array[i].pending_upstream_write.read_bytes_size
                > client_array[i].pending_upstream_write.offset_bytes;

            if (client_array[i].current_state != CONN_DRAIN_CLIENT && !upstream_pending) {
                FD_SET(client_array[i].connection_fd, &read_fds);
            }

            if (client_array[i].connection_fd > max_fd) {
                max_fd = client_array[i].connection_fd;
            }
            if (client_array[i].upstream_fd != -1) {
                if (client_array[i].current_state == CONN_RELAY) {
                    FD_SET(client_array[i].upstream_fd, &read_fds);
                }
                if (client_array[i].current_state == CONN_CONNECTING || upstream_pending) {
                    FD_SET(client_array[i].upstream_fd, &write_fds);
                }
                if (client_array[i].upstream_fd > max_fd) {
                    max_fd = client_array[i].upstream_fd;
                }
            }

            if (client_array[i].current_state == CONN_DRAIN_CLIENT) {
                FD_SET(client_array[i].connection_fd, &write_fds);
            }
        }

        int select_response = select(max_fd + 1, &read_fds, &write_fds, NULL, NULL);

        if (select_response < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Select error");
            continue;
        } else if (select_response == 0) {
            printf("Timeout! You took too long.\n");
        }

        for (int i = 0; i < client_count; i++) {
            if (client_array[i].connection_fd == listening_socket &&
                FD_ISSET(client_array[i].connection_fd, &read_fds)) {
                int connection_fd = accept(listening_socket, NULL, NULL);
                if (connection_fd == -1) {
                    perror("Accept Connection");
                    continue;
                }

                if (set_nonblock(connection_fd) == -1) {
                    close(connection_fd);
                    perror("FD Flag changing issue");
                    continue;
                }

                memset(&client_array[client_count], 0, sizeof(Connection));
                client_array[client_count].connection_fd = connection_fd;
                client_array[client_count].upstream_fd = -1;
                client_array[client_count].current_state = CONN_CLIENT_REQUEST;
                client_count++;
            }
            else if (client_array[i].current_state == CONN_DRAIN_CLIENT &&
                     FD_ISSET(client_array[i].connection_fd, &write_fds)) {
                if (drain_write_buffer(i, client_array, &client_count) == 0) {
                    i--;
                    continue;
                }
            }
            else if (client_array[i].current_state == CONN_CONNECTING &&
                     client_array[i].upstream_fd != -1 &&
                     FD_ISSET(client_array[i].upstream_fd, &write_fds)) {
                if (finish_connect(i, client_array, &client_count) == 0) {
                    i--;
                    continue;
                }
            }
            else if (client_array[i].current_state == CONN_RELAY &&
                     client_array[i].upstream_fd != -1 &&
                     client_array[i].pending_upstream_write.read_bytes_size
                         > client_array[i].pending_upstream_write.offset_bytes &&
                     FD_ISSET(client_array[i].upstream_fd, &write_fds)) {
                if (drain_upstream_write(i, client_array, &client_count) == 0) {
                    i--;
                    continue;
                }
            }
            else if (FD_ISSET(client_array[i].connection_fd, &read_fds)) {
                if (client_array[i].current_state == CONN_CLIENT_REQUEST) {
                    if (handle_client(i, client_array, &client_count) == 0) {
                        i--;
                        continue;
                    }
                } else if (client_array[i].current_state == CONN_RELAY) {
                    if (handle_client_relay(i, client_array, &client_count) == 0) {
                        i--;
                        continue;
                    }
                } else {
                    close_fd(i, client_array, &client_count);
                    i--;
                    continue;
                }
            }
            else if (client_array[i].upstream_fd != -1 &&
                     client_array[i].current_state == CONN_RELAY &&
                     FD_ISSET(client_array[i].upstream_fd, &read_fds)) {
                if (handle_upstream(i, client_array, &client_count) == 0) {
                    i--;
                    continue;
                }
            }
        }
    }

    close(listening_socket);
    printf("Server shut down cleanly.\n");
    return 0;
}
