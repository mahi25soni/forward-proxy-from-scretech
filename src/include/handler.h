#ifndef HANDLER_H
#define HANDLER_H

#include <parser.h>

void handle_http_request(int fd, const char *raw_request, ProxyPathData *proxy);

#endif