#include <handler.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

volatile sig_atomic_t keep_running = 1;

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

void *handling_client_per_thread(void *arg) {
    int fd = *(int *)arg;
    free(arg);

    while (1) {
        char buf[2000];
        ssize_t read_bytes = read(fd, buf, sizeof(buf) - 1);
        if (read_bytes < 0) {
            perror("read");
            close(fd);
            return NULL;
        }
        if (read_bytes == 0) {
            printf("[DISCONNECT] fd=%d\n", fd);
            close(fd);
            return NULL;
        }
        buf[read_bytes] = '\0';

        handle_http_request(fd, buf);
    }

    return NULL;
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

    while (keep_running) {
        int *connection_fd = malloc(sizeof(int));
        if (connection_fd == NULL) {
            perror("Malloc");
            continue;
        }

        *connection_fd = accept(listening_socket, NULL, NULL);
        if (*connection_fd == -1) {
            perror("Accept Connection");
            free(connection_fd);
            continue;
        }

        pthread_t t;
        if (pthread_create(
                &t,
                NULL,
                &handling_client_per_thread,
                connection_fd) != 0) {
            perror("Thread Creation");
            close(*connection_fd);
            free(connection_fd);
            continue;
        }
        pthread_detach(t);
    }

    close(listening_socket);
    printf("Server shut down cleanly.\n");
    return 0;
}
