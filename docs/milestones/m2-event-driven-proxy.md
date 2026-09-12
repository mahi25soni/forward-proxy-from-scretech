# Milestone 2 — Event-Driven Proxy

## Status

✅ Done — verified 2026-08-22 (macOS: `select`, not epoll)

## Definition of done

Many simultaneous HTTP clients; one slow connection does not block the others.

```bash
# fast client should finish while slow origin is still waiting
curl --proxy localhost:8080 http://example.com -o /tmp/a.html -w "A time=%{time_total}\n"
curl --proxy localhost:8080 http://httpbin.org/delay/5 -o /tmp/b.html -w "B time=%{time_total}\n"
```

Also: given an fd from `select`, we know which `Connection` slot it belongs to and what `current_state` that slot is in.

## What we built

- `Connection` — client fd + upstream fd + `ConnState` + optional pending client write + saved method/path/host
- Non-blocking client and upstream sockets (`set_nonblock`)
- `select` read set: listen, clients, upstream when `CONN_RELAY`
- `select` write set: upstream when `CONN_CONNECTING`, client when `CONN_DRAIN_CLIENT`
- Split handlers: `handle_client`, `finish_connect`, `handle_upstream`, `drain_write_buffer`
- `close_fd` closes the pair and compacts the array
- Files: `src/include/connection.h`, `src/include/proxy.h`, `src/proxy/connection.c`, `src/proxy/proxy.c`; loop stays in `src/main.c`

## What we reused

- M1 parse + origin-form `create_http_request` + `http_error`
- Existing `select` loop (extended to upstream + write fds)
- Static file path in `handle_http_request` for non-proxy GETs

## What we learned

- `select` does not `read`/`write`; it only reports readiness. The loop must return between syscalls.
- Same fd can mean two jobs (client readable = new GET vs writable = drain). That is why `ConnState` exists.
- Non-blocking `connect` uses `EINPROGRESS` + writable + `SO_ERROR`, not “writable means success.”
- Short/`EAGAIN` `write` to curl requires bytes on the `Connection` (`pending_write_data`), not a stack buffer.
- macOS: event-driven M2 is `select`, not epoll (same idea: fd → slot → state).

## How to verify

```bash
gcc -Wall -Wextra -I src/include src/*.c src/http/*.c src/error-handling/*.c src/proxy/*.c -o server
./server

curl --proxy localhost:8080 http://example.com
curl --proxy localhost:8080 http://example.com -o /tmp/a.html -w "A time=%{time_total}\n"
curl --proxy localhost:8080 http://httpbin.org/delay/5 -o /tmp/b.html -w "B time=%{time_total}\n"
```

## Known limitations

- `getaddrinfo` still blocks the process
- `create_http_request` still one full `write` (no retry on short write)
- One 2048-byte pending buffer — not full M3 backpressure/queue
- Client request still one `read` into 2000 bytes
- No HTTPS / CONNECT (M4)
