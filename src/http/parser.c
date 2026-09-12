
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
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

int get_proxy_path_data(const char *raw_path, const char *method, ProxyPathData *out)
{
    memset(out, 0, sizeof(*out));
    if (!raw_path || !method)
        return -1;

    strncpy(out->method, method, sizeof(out->method) - 1);

    const char *scheme_end = strstr(raw_path, "://");
    if (!scheme_end)
        return -1;

    size_t scheme_len = (size_t)(scheme_end - raw_path);
    if (scheme_len == 0 || scheme_len >= sizeof(out->scheme))
        return -1;
    memcpy(out->scheme, raw_path, scheme_len);
    out->scheme[scheme_len] = '\0';

    const char *host_start = scheme_end + 3;
    const char *path_start = strchr(host_start, '/');
    const char *host_end = path_start ? path_start : host_start + strlen(host_start);

    const char *colon = memchr(host_start, ':', (size_t)(host_end - host_start));
    const char *name_end = colon ? colon : host_end;

    size_t host_len = (size_t)(name_end - host_start);
    if (host_len == 0 || host_len >= sizeof(out->host))
        return -1;
    memcpy(out->host, host_start, host_len);
    out->host[host_len] = '\0';

    if (colon) {
        out->port = atoi(colon + 1);
        if (out->port <= 0)
            return -1;
    } else if (strcasecmp(out->scheme, "https") == 0) {
        out->port = 443;
    } else {
        out->port = 80;
    }

    if (path_start)
        strncpy(out->path, path_start, sizeof(out->path) - 1);
    else
        strncpy(out->path, "/", sizeof(out->path) - 1);

    return 0;
}

int get_connect_target(const char *authority, ProxyPathData *out)
{
    memset(out, 0, sizeof(*out));
    if (!authority || authority[0] == '\0')
        return -1;

    strncpy(out->method, "CONNECT", sizeof(out->method) - 1);

    const char *colon = strrchr(authority, ':');
    if (!colon) {
        if (strlen(authority) >= sizeof(out->host))
            return -1;
        strncpy(out->host, authority, sizeof(out->host) - 1);
        out->port = 443;
        return 0;
    }

    size_t host_len = (size_t)(colon - authority);
    if (host_len == 0 || host_len >= sizeof(out->host))
        return -1;
    memcpy(out->host, authority, host_len);
    out->host[host_len] = '\0';

    out->port = atoi(colon + 1);
    if (out->port <= 0)
        return -1;

    return 0;
}