#include <error.h>
#include <string.h> // Required for strcpy or memcpy
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

const struct statusCodeToReasonStruct status_code_list[] = {
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {413, "Payload Too Large"},
    {500, "Internal Server Error"},
    {502, "Bad Gateway"},
    {501, "Not Implemented"},
    {505, "HTTP Version Not Supported"}
};

int total_status_code = sizeof(status_code_list) / sizeof(struct statusCodeToReasonStruct);

const char *getCodeMessage(int inputcode) {
    for(int i = 0; i < total_status_code; i++){
        if(status_code_list[i].code == inputcode ) {
            return status_code_list[i].official_reason;
        }
    }

    return NULL;
}

void http_error(int fd, int status_code, char *error_message){
    const char *reason = getCodeMessage(status_code);
    size_t content_length = strlen(error_message);

    // calcualte size of the response buffer
    int response_size = snprintf(
        NULL,
        0,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        status_code, reason, content_length, error_message
    );

    // initialise the response
    char buf[response_size+1];
    snprintf(buf, response_size+1, 
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "%s",
        status_code, reason, content_length, error_message
    );

    write(fd, buf, response_size);
}

void http_response(int fd, FILE *fp, char *content_type){
    
    // calculate content length
    fseek(fp, 0, SEEK_END);
    long content_length = ftell(fp);
    fseek(fp, 0, SEEK_SET);   

    // calculate response length
    int response_size = snprintf(
        NULL,
        0,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        content_type, content_length
    );

    // 
    char buf[response_size + 1];
    snprintf(
        buf,
        response_size + 1,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        content_type, content_length
    );

    write(fd, buf, response_size);
}

int create_http_request(int fd, char *method, char *path, char *host) {
    int request_size = snprintf(
        NULL, 0,
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        method, path, host
    );
    if (request_size < 0)
        return -1;

    char request[request_size + 1];
    if (snprintf(request, sizeof(request),
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        method, path, host) != request_size)
        return -1;

    ssize_t written = write(fd, request, (size_t)request_size);
    if (written != request_size) {
        perror("upstream write");
        return -1;
    }

    printf("[UPSTREAM] wrote %d bytes\n", request_size);
    return 0;
}