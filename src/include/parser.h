#ifndef PARSER_H
#define PARSER_H

typedef struct {
    char name[64];
    char value[256];
} HttpHeader;

typedef struct {
    char method[16];
    char path[256];
    char version[16];

    HttpHeader headers[32];
    int header_count;
} HttpRequest;

typedef struct {
    char method[16];
    char scheme[16];
    char host[256];
    char path[256];
    int port;
} ProxyPathData;

int parse_http_request(char *buf, HttpRequest *req);
int get_proxy_path_data(const char *raw_path, const char *method, ProxyPathData *out);

#endif