# Milestone 4 — HTTPS via CONNECT

## Status

✅ Done — verified 2026-09-12

## Definition of done

```bash
curl --proxy localhost:8080 https://example.com
```

Proxy tunnels TLS without implementing TLS. HTTP proxy regression still works.

## What we built

- `get_connect_target()` — parse `example.com:443` → host + port
- CONNECT branch in `handle_http_request`
- `send_connect_established()` — `HTTP/1.1 200 Connection Established` to client
- `handle_client_relay()` — `read(client)` → `write(upstream)`
- `pending_upstream_write` + `drain_upstream_write()` — backpressure client→upstream
- `main.c` — relay dispatch in `CONN_RELAY`; upstream `write_fds` when draining

## What we reused

- M2 connect path (`getaddrinfo`, `EINPROGRESS`, `finish_connect`, `SO_ERROR`)
- M3 client drain pattern (`pending_write_data`, `drain_write_buffer`) mirrored for upstream
- `handle_upstream` for server → client direction unchanged

## What we learned

- After `200`, proxy stops speaking HTTP; every byte is blind copy
- Two fds, two directions: client→upstream and upstream→client are separate handlers
- curl blocks on `200` before sending ClientHello

## How to verify

```bash
./server
curl -v --proxy localhost:8080 https://example.com   # expect 200, TLS, HTML
curl --proxy localhost:8080 http://example.com      # regression
```

## Known limitations

- No `is_connect` flag (branches on `strcmp(method, "CONNECT")`)
- First EOF closes both sides immediately (no half-close)
- No connect/idle timeouts (M5)
- No config file or port ACL (M6)
