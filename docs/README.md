# Forward Proxy (C)

A small HTTP forward proxy built milestone by milestone. It runs on `localhost:8080` and forwards traffic to real websites.

**Project status:** Paused after milestone 4. HTTP and HTTPS both work.

## What it does

```bash
./server

curl --proxy localhost:8080 http://example.com    # HTTP
curl --proxy localhost:8080 https://example.com   # HTTPS (via CONNECT tunnel)
```

The proxy sits between curl and the internet. For HTTP it rewrites the request and relays the response. For HTTPS it opens a TCP tunnel and copies encrypted bytes — it never decrypts TLS.

## What we built

| Milestone | What it added |
|-----------|----------------|
| [M1 — Basic HTTP](milestones/m1-basic-http-forwarding.md) | Parse proxy requests, connect upstream, forward HTTP |
| [M2 — Event-driven](milestones/m2-event-driven-proxy.md) | `select`, non-blocking sockets, many clients at once |
| M3 — Streaming | Partial read/write, backpressure, pending buffers *(in code, no separate doc)* |
| [M4 — HTTPS](milestones/m4-https-via-connect.md) | `CONNECT` tunnel, bidirectional relay for TLS |

Not built: timeouts, config file, stress testing (planned as M5–M7 in `path.txt`).

## Docs

- **[milestones/](milestones/)** — what we shipped at each step
- **[LEARNING.md](LEARNING.md)** — full reference: architecture, functions, edge cases, glossary

Build:

```bash
gcc -Wall -Wextra -I src/include src/*.c src/http/*.c src/error-handling/*.c src/proxy/*.c -o server
```
