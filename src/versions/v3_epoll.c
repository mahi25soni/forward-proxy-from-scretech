#include <handler.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/epoll.h>

volatile sig_atomic_t keep_running = 1;
struct epoll_event ev;

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

int handle_client(int epfd, int fd) {
    char buf[2000];
    ssize_t read_bytes = read(fd, buf, sizeof(buf) - 1);
    if (read_bytes < 0) {
        perror("read");
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return 0;
    }
    if (read_bytes == 0) {
        printf("[DISCONNECT] fd=%d\n", fd);
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return 0;
    }
    buf[read_bytes] = '\0';

    handle_http_request(fd, buf);

    return 1;
}

int main(void) {
    setup_signals();

    int listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_socket == -1) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listening_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(listening_socket);
        perror("bind");
        exit(1);
    }

    if (listen(listening_socket, 200) < 0) {
        close(listening_socket);
        perror("listen");
        exit(1);
    }

    int epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        close(listening_socket);
        exit(1);
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = listening_socket;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listening_socket, &ev) == -1) {
        perror("epoll_ctl ADD listen");
        close(epfd);
        close(listening_socket);
        exit(1);
    }

    while (keep_running) {
        struct epoll_event events[10];
        int total_events = epoll_wait(epfd, events, 10, -1);

        if (total_events == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            continue;
        }

        for (int i = 0; i < total_events; i++) {
            if (events[i].data.fd == listening_socket) {
                int connection_fd = accept(listening_socket, NULL, NULL);
                if (connection_fd == -1) {
                    perror("accept");
                    continue;
                }

                memset(&ev, 0, sizeof(ev));
                ev.events = EPOLLIN;
                ev.data.fd = connection_fd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, connection_fd, &ev) == -1) {
                    perror("epoll_ctl ADD client");
                    close(connection_fd);
                    continue;
                }
            } else {
                int client_fd = events[i].data.fd;
                if (handle_client(epfd, client_fd) == 0) {
                    continue;
                }
            }
        }
    }

    close(epfd);
    close(listening_socket);
    printf("Server shut down cleanly.\n");
    return 0;
}
