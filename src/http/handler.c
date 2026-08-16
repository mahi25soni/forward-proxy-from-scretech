#include <handler.h>
#include <parser.h>
#include <error.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

void handle_http_request(int fd, const char *raw_request, ProxyPathData *proxy) {
    HttpRequest req;

    if (parse_http_request((char *)raw_request, &req) < 0) {
        return;
    }

    printf("the fd is %d \n", fd);
    printf("method is %s\n", req.method);
    printf("path is %s\n", req.path);

    if (strncmp(req.path, "http", 4) == 0) {
        if (get_proxy_path_data(req.path, req.method, proxy) < 0) {
            http_error(fd, 400, "Error in request");
            return;
        }
        return;
    }

    if (strcasecmp(req.method, "GET") != 0) {
        return;
    }

    char *path = NULL;
    char *content_type = NULL;

    if (strcasecmp(req.path, "/") == 0) {
        path = "public/index.html";
        content_type = "text/html";
    } else if (strcasecmp(req.path, "/style.css") == 0) {
        path = "public/style.css";
        content_type = "text/css";
    } else if (strcasecmp(req.path, "/app.js") == 0) {
        path = "public/app.js";
        content_type = "application/javascript";
    }

    if (content_type == NULL || path == NULL) {
        http_error(fd, 400, "Error in request");
        return;
    }

    FILE *file_ptr = fopen(path, "rb");
    if (file_ptr == NULL) {
        http_error(fd, 403, "File don't exists or dont' have enough permission");
        return;
    }

    http_response(fd, file_ptr, content_type);

    char file_buf[2048];
    size_t nbytes;
    while ((nbytes = fread(file_buf, 1, sizeof(file_buf), file_ptr)) > 0) {
        write(fd, file_buf, nbytes);
    }

    fclose(file_ptr);
}