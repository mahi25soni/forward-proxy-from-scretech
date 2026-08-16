# Milestone 1 — Basic HTTP Forwarding

## Status

✅ Done — verified 2026-08-16

## Definition of done

```bash
curl --proxy localhost:8080 http://example.com
```

Request path: curl → proxy → origin server → proxy → curl

## What we built

- `get_proxy_path_data()` — parse absolute URL into scheme, host, port, path, method
- `create_http_request()` — build origin-form request, `write()` to upstream
- Proxy path in `handle_client()` — `getaddrinfo`, connect loop, relay loop
- 502 responses on DNS failure, connect failure, upstream write/read errors

## What we reused

- `select()` event loop in `main.c` (clients only — upstream handled inline)
- `parse_http_request()` for request line + headers
- `http_error()` for client-facing errors
- Static file handler kept for non-proxy requests (local paths)

## What we learned

- Proxy-form uses full URL in request line; origin-form uses path only
- Same process is TCP server (client fd) and TCP client (upstream fd)
- Upstream fd does not go into `select` for M1 — blocking relay inside `handle_client`
- `Connection: close` on upstream request simplifies M1 lifecycle

## How to verify

```bash
gcc -Wall -Wextra -I src/include src/*.c src/http/*.c src/error-handling/*.c -o server
./server

curl --proxy localhost:8080 http://example.com
curl --proxy localhost:8080 http://thishostdoesnotexist.invalid/   # expect 502
```

## Known limitations

- Blocking I/O — one slow proxy request blocks `handle_client` (M2)
- No partial write handling on relay (M3)
- Minimal upstream request — only method, path, Host, Connection (no header forwarding)
- No HTTPS / CONNECT (M4)
- Error paths on relay may leave client fd in `client_array` (`return 1` vs `close_fd`)
