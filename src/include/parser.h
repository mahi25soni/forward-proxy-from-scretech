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


int parse_http_request(char *buf, HttpRequest *req);

#endif