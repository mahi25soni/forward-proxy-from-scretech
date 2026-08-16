#ifndef ERROR_H
#define ERROR_H

#include <stdio.h>

struct statusCodeToReasonStruct {
    int code;
    char official_reason[100];
};

extern const struct statusCodeToReasonStruct status_code_list[];

void http_error(int fd, int status_code, char *error_message);
void http_response(int fd, FILE *fp, char *content_type);
int create_http_request(int fd, char *method, char *path, char *host);
#endif