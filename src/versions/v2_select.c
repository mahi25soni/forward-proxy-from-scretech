#include <handler.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>

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

        handle_http_request(fd, buf);

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
