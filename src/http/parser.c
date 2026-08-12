
#include <string.h>
#include <stdio.h>
#include <parser.h>


int parse_http_request(char *buf, HttpRequest *req)
{
    memset(req, 0, sizeof(HttpRequest));

    char *line = strtok(buf, "\r\n");
    if (!line)
        return -1;

    if (sscanf(line, "%15s %255s %15s",
               req->method,
               req->path,
               req->version) != 3)
    {
        return -1;
    }

    while ((line = strtok(NULL, "\r\n")) != NULL)
    {
        if (*line == '\0')
            break;

        char *colon = strchr(line, ':');
        if (!colon)
            continue;

        *colon = '\0';

        strncpy(req->headers[req->header_count].name,
                line,
                sizeof(req->headers[0].name) - 1);

        strncpy(req->headers[req->header_count].value,
                colon + 2,
                sizeof(req->headers[0].value) - 1);

        req->header_count++;
    }

    return 0;
}