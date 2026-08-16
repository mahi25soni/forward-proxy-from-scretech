# Architecture

## M1 — Basic HTTP Forwarding (current)

One proxy transaction per `handle_client` call. Blocking connect, write, read relay.

```
                    select() watches
                    ┌─────────────────┐
  curl ──TCP──►     │ listening +     │
                    │ client fds only │
                    └────────┬────────┘
                             │ client readable
                             ▼
                    handle_client(client_fd)
                      read(client)           ← proxy-form HTTP request
                      parse → host/port/path
                      connect(upstream_fd)   ← NOT in select
                      write(upstream)        ← origin-form request
                      read(upstream) ──► write(client)
                      close(upstream)
                      close_fd(client)
```

### Fd ownership

| fd | Role | Lifetime |
|----|------|----------|
| `listening_socket` | Accept clients | Process lifetime |
| `client_fd` | curl ↔ proxy | From accept until relay done or error |
| `upstream_fd` | proxy ↔ origin | From connect until relay done or error |

### Request transformation

```text
Client sends:  GET http://example.com/foo HTTP/1.1
Upstream gets: GET /foo HTTP/1.1 + Host: example.com
```

### M2 preview

Upstream fds join the event loop (epoll). Per-connection state maps fd → client/upstream pair. Non-blocking I/O so slow connections don't block others.
