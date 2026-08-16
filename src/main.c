#include <handler.h>
#include <parser.h>
#include <error.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <netdb.h>
#include <arpa/inet.h>

volatile sig_atomic_t keep_running = 1;
fd_set read_fds;

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

void close_fd(int fd, int fd_index, int client_array[], int *client_count){
    client_array[fd_index] = client_array[*client_count - 1];
    (*client_count) -= 1;
    close(fd);

}
int handle_client(int fd, int fd_index, int client_array[], int *client_count){
        char buf[2000];
        ssize_t read_bytes = read(fd, buf, sizeof(buf) - 1);
        if (read_bytes < 0) {
            perror("read");
            close_fd(fd, fd_index, client_array, client_count);
            return 0;
        }
        if (read_bytes == 0) {
            printf("[DISCONNECT] fd=%d\n", fd);
            close_fd(fd, fd_index, client_array, client_count);
            return 0;
        }
        buf[read_bytes] = '\0';

        ProxyPathData proxy = {0};
        handle_http_request(fd, buf, &proxy);

        if (proxy.host[0] != '\0') {
            char port_str[8];
            snprintf(port_str, sizeof(port_str), "%d", proxy.port);
            
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
            for (p = res; p != NULL; p = p->ai_next) {
                int try_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (try_fd == -1) {
                    perror("upstream socket");
                    continue;
                }

                if (connect(try_fd, p->ai_addr, p->ai_addrlen) == 0) {
                    upstream_fd = try_fd;
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

            printf("[UPSTREAM] connected fd=%d to %s:%d\n",
                   upstream_fd, proxy.host, proxy.port);

            if (create_http_request(upstream_fd, proxy.method, proxy.path, proxy.host) < 0) {
                close(upstream_fd);
                http_error(fd, 502, "Could not send request to upstream");
                return 1;
            }

            // Let's read the data
            char proxy_read_buf[4048];
            ssize_t read_size;
            while ((read_size = read(upstream_fd, proxy_read_buf, sizeof(proxy_read_buf))) > 0) {
                if ( write(fd, proxy_read_buf, read_size) == -1){
                    close(upstream_fd);
                    perror("read proxy error");
                    http_error(fd, 502, "Error in reading proxy data");
                    return 1;
                };
            }
            if (read_size < 0){
                close(upstream_fd);
                perror("read proxy error");
                http_error(fd, 502, "Error in reading proxy data");
                return 1;
            }
            close(upstream_fd);
            close_fd(fd, fd_index, client_array, client_count);
            return 0;
        }

        return 1;
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


    // put listening socket into read_fds
    int client_array[1024];
    int client_count = 0;
    client_array[client_count++] = listening_socket;
    while (keep_running) {
        FD_ZERO(&read_fds);

        int max_fd = -1;
        for(int i = 0; i < client_count; i++){
            FD_SET(client_array[i], &read_fds);
            if(client_array[i] > max_fd){
                max_fd = client_array[i];
            }
        }

        int select_response = select(max_fd+1, &read_fds, NULL, NULL, NULL);

        if (select_response < 0) {
            perror("Select error");
        } else if (select_response == 0) {
            printf("Timeout! You took too long.\n");
        }

        for (int i = 0; i<client_count; i++) {
            if(client_array[i] == listening_socket && FD_ISSET(client_array[i], &read_fds)){
                int connection_fd = accept(listening_socket, NULL, NULL);
                if (connection_fd == -1) {
                    perror("Accept Connection");
                    continue;
                }

                client_array[client_count++] = connection_fd;
            }
            else if (FD_ISSET(client_array[i], &read_fds)){
                if ( handle_client(client_array[i], i, client_array, &client_count) == 0){
                    continue;
                };
            }
        }
    }

    close(listening_socket);
    printf("Server shut down cleanly.\n");
    return 0;
}

