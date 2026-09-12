# Forward Proxy — Complete Learning Reference

Single doc consolidating glossary, architecture, flows, functions, edge cases, decisions, and milestone learnings.  
**Status:** M1–M4 done. Project paused before M5. Master spec: [`path.txt`](../path.txt).

---

## 1. What we built (milestones)

| Milestone | DoD | Status |
|-----------|-----|--------|
| M1 Basic HTTP | `curl --proxy localhost:8080 http://example.com` | ✅ |
| M2 Event-driven | Many clients; one slow conn doesn't block others | ✅ |
| M3 Streaming + backpressure | Bounded buffers; partial I/O | ✅ (in code) |
| M4 HTTPS CONNECT | `curl --proxy localhost:8080 https://example.com` | ✅ |
| M5 Failure / timeouts | No hangs, no leaks on errors | ❌ |
| M6 Operational | Config, logging, limits | ❌ |
| M7 Validation | Stress + benchmarks | ❌ |

### M1 learnings
- Proxy-form: full URL in request line (`GET http://host/path`)
- Origin-form: path only upstream (`GET /path` + `Host:`)
- Same process is TCP server (client) and TCP client (upstream)
- M1 upstream fd not in `select` — blocking relay inside `handle_client`

### M2 learnings
- `select` reports readiness; handlers do one non-blocking step and return
- `ConnState` answers "what step" when an fd wakes up
- `EINPROGRESS` + writable + `SO_ERROR` for async connect
- Pending write must live on `Connection`, not stack (locals die on `return`)
- macOS: `select`, not epoll — same mental model

### M3 learnings (implemented)
- Partial `read`/`write`; `EAGAIN` is not fatal
- One 2048-byte pending buffer per direction; pause read while draining
- `offset_bytes` tracks partial writes

### M4 learnings
- CONNECT request-target is `host:port`, not a URL
- `200 Connection Established` → `connection_fd` only; upstream gets no HTTP
- After `200`, blind byte relay both ways; proxy never sees TLS plaintext
- `handle_client_relay` (client→upstream) + `handle_upstream` (upstream→client)

---

## 2. Architecture

### M1 (historical)

```
curl ──TCP──► select(listen + clients only)
                    │
                    ▼ handle_client (blocking)
              parse → connect → write HTTP upstream
              read upstream → write client → close
```

### M2+ (event loop)

```
select()
  read:  listen + clients + upstream (if RELAY)
  write: upstream (CONNECTING or pending_upstream) + client (DRAIN_CLIENT)
       │
       ├─ accept           → CONN_CLIENT_REQUEST
       ├─ drain client     → drain_write_buffer
       ├─ drain upstream   → drain_upstream_write
       ├─ upstream write   → finish_connect (if CONNECTING)
       ├─ client read      → handle_client (CLIENT_REQUEST) or handle_client_relay (RELAY)
       └─ upstream read    → handle_upstream (RELAY)
```

### M4 CONNECT tunnel

```
CONNECT example.com:443
  get_connect_target → getaddrinfo → connect (sync or EINPROGRESS)
  send_connect_established → connection_fd
  CONN_RELAY:
    handle_client_relay:  read(client)  → write(upstream)
    handle_upstream:      read(upstream) → write(client)
```

### Request transformation (HTTP proxy)

```text
Client:   GET http://example.com/foo HTTP/1.1
Upstream: GET /foo HTTP/1.1
          Host: example.com
          Connection: close
```

### Fd ownership

| fd | Role | In select when |
|----|------|----------------|
| `listening_socket` | Accept | Always read (slot 0) |
| `connection_fd` | curl ↔ proxy | Read (unless draining upstream pending); write if `CONN_DRAIN_CLIENT` |
| `upstream_fd` | proxy ↔ origin | Read if `CONN_RELAY`; write if `CONN_CONNECTING` or `pending_upstream_write` |

### Pending buffers

| Field | Direction | Drain function | State side-effect |
|-------|-----------|----------------|-------------------|
| `pending_write_data` | upstream → client | `drain_write_buffer` | `CONN_DRAIN_CLIENT` while draining |
| `pending_upstream_write` | client → upstream | `drain_upstream_write` | stays `CONN_RELAY`; pause client read |

### `Connection` struct

```c
connection_fd, upstream_fd, current_state
pending_write_data, pending_upstream_write
method[16], path[256], host[256]   // saved for async connect
```

### `ConnState`

| State | Meaning |
|-------|---------|
| `CONN_CLIENT_REQUEST` | Waiting for / parsing first HTTP request from client |
| `CONN_CONNECTING` | Non-blocking connect in progress |
| `CONN_RELAY` | Relaying bytes (HTTP response or tunnel) |
| `CONN_DRAIN_CLIENT` | Finishing partial write to client |

---

## 3. State machine flow

```text
accept → CONN_CLIENT_REQUEST
  handle_client: read, parse, connect
       ├─ connect() == 0  → create_http_request (HTTP) or send_connect_established (CONNECT) → RELAY
       └─ EINPROGRESS     → CONNECTING → finish_connect → RELAY

CONN_RELAY
  handle_upstream / handle_client_relay: one read → one write
       ├─ full write     → stay RELAY
       ├─ short / EAGAIN → stash → DRAIN_* (client) or pending_upstream (tunnel)
       └─ read 0 / error → close_fd

CONN_DRAIN_CLIENT
  drain_write_buffer until empty → RELAY

close_fd: close both fds, swap-with-last, client_count--
```

### Dispatch order in `main` (important)

Drain and `finish_connect` **before** treating client readable as a new HTTP request — same fd must not be parsed as HTTP while bytes are still owed to curl.

---

## 4. Glossary

| Term | Definition |
|------|------------|
| **absolute-form / proxy-form** | `GET http://example.com/path HTTP/1.1` — full URL in request line |
| **origin-form** | `GET /path HTTP/1.1` — what origin server expects |
| **upstream fd** | TCP socket proxy → origin server |
| **non-blocking socket** | `O_NONBLOCK`; syscalls return immediately; `EAGAIN`/`EWOULDBLOCK` if not ready |
| **EINPROGRESS** | Async `connect` started; wait for writable + check `SO_ERROR` |
| **SO_ERROR** | `getsockopt(SOL_SOCKET, SO_ERROR)` — real connect result after writable |
| **relay** | Copy bytes without parsing (HTTP body or TLS ciphertext) |
| **backpressure** | Stop reading when peer can't accept writes; buffer one chunk |
| **CONNECT** | `CONNECT host:port HTTP/1.1` — open TCP tunnel for HTTPS |
| **authority** | `host:port` in CONNECT request-target |
| **TCP tunnel** | After `200`, opaque bidirectional byte copy |
| **fd set rebuild** | `FD_ZERO` + `FD_SET` every loop (`select` mutates sets) |

---

## 5. Functions reference

### `main.c`
| Function | Purpose |
|----------|---------|
| `setup_signals` / `handle_interrupt` | SIGINT/SIGTERM → `keep_running = 0` |
| `main` | Bind :8080, rebuild fd sets, `select`, dispatch handlers |

### `connection.c`
| Function | Purpose |
|----------|---------|
| `set_nonblock` | `fcntl(O_NONBLOCK)` on accepted / upstream sockets |
| `close_fd` | Close client + upstream; compact `client_array`; caller does `i--` |

### `proxy.c`
| Function | Purpose |
|----------|---------|
| `handle_client` | `CONN_CLIENT_REQUEST` only: read request, parse, start connect |
| `finish_connect` | Upstream writable: `SO_ERROR`; HTTP → `create_http_request`; CONNECT → `send_connect_established` |
| `handle_upstream` | `CONN_RELAY`: `read(upstream)` → `write(client)` |
| `handle_client_relay` | `CONN_RELAY`: `read(client)` → `write(upstream)` |
| `stash_pending_write` | Buffer chunk for client; → `CONN_DRAIN_CLIENT` |
| `stash_pending_upstream_write` | Buffer chunk for upstream; stay `CONN_RELAY` |
| `drain_write_buffer` | Flush `pending_write_data` to client |
| `drain_upstream_write` | Flush `pending_upstream_write` to upstream |
| `send_connect_established` | (static) Write `HTTP/1.1 200 Connection Established` to client |

**Return convention:** `0` = slot removed (`i--`); `1` = slot alive, back to `select`.

### `parser.c`
| Function | Purpose |
|----------|---------|
| `parse_http_request` | Split request line + headers into `HttpRequest` |
| `get_proxy_path_data` | Parse `http://host:port/path` → `ProxyPathData` |
| `get_connect_target` | Parse `host:port` authority → `ProxyPathData` (default port 443) |

### `handler.c`
| Function | Purpose |
|----------|---------|
| `handle_http_request` | Route CONNECT / proxy URL / static files; fill `ProxyPathData` |

### `error.c`
| Function | Purpose |
|----------|---------|
| `http_error` | Send HTML error response to client |
| `http_response` | Send 200 + headers for static files |
| `create_http_request` | Build origin-form request; single `write` upstream |
| `getCodeMessage` | Map status code → reason phrase |

---

## 6. Edge cases

### Handled

| Case | Fix |
|------|-----|
| Loop blocked on one client | Non-blocking I/O; return to `select` after one step |
| `EAGAIN` on read | Not fatal; wait for next `select` wakeup |
| Upstream readable ≠ new HTTP request | `handle_upstream`, don't parse HTML as proxy request |
| Same fd, different jobs | `ConnState` disambiguates (parse vs drain vs relay) |
| `connect` `EINPROGRESS` | `CONN_CONNECTING`; save method/path/host on slot |
| Failed connect still writable | Check `SO_ERROR` before assuming success |
| Partial write to client | `stash_pending_write` + `offset_bytes` |
| `EAGAIN` on write to client | Stash with `offset = 0` |
| Stack buffer lost on return | Pending bytes on `Connection` struct |
| Don't read origin while draining client | Remove upstream from read set during `CONN_DRAIN_CLIENT` |
| Don't read client while draining upstream | Skip client read set when `pending_upstream_write` active |
| Client hangup mid-relay | `read` returns 0 → `close_fd` |
| Origin hangup | `read` returns 0 → `close_fd` |
| `close_fd` during for-loop | `i--` after swap-with-last |
| `select` + Ctrl+C | `EINTR` → continue |
| CONNECT vs GET after connect | Branch on method; `200` vs `create_http_request` |
| Tunnel needs client→upstream relay | `handle_client_relay` in `CONN_RELAY` |

### Still unhandled (M5+)

| Case | Notes |
|------|-------|
| Blocking `getaddrinfo` | Can stall entire event loop |
| Short write in `create_http_request` | Single `write`, no retry |
| Client request split across reads | One `read` into 2000 bytes |
| Half-close / graceful tunnel teardown | First EOF closes both sides |
| Connect timeout | `CONN_CONNECTING` can hang forever |
| Idle timeout | Open connections never expire |
| No `is_connect` flag | Uses `strcmp(method, "CONNECT")` |
| IPv6 authority `[::1]:443` | Not parsed |
| Queue of multiple pending chunks | One 2048-byte buffer per direction max |
| Header forwarding to upstream | Only method, path, Host, Connection |
| Port ACL / open-proxy security | Any host:port allowed |

---

## 7. Architecture decisions (ADRs)

| ADR | Decision | Why |
|-----|----------|-----|
| 001 | `select` for M1 | Smallest path; defer epoll |
| 002 | Upstream outside select in M1 | Sync transaction per request |
| 003 | `select` not epoll for M2 | macOS portable; same fd→slot→state model |
| 004 | `client_array[]` of `Connection` | State survives across `select` returns |
| 005 | `ConnState` enum | Same fd means different jobs at different times |
| 006 | `EINPROGRESS` + `SO_ERROR` | Non-blocking connect without blocking loop |
| 007 | One pending buffer + pause read | M2 no-block; M3 bounded memory |
| 008 | Split `src/proxy/` | `main.c` = loop only |
| 009 | `get_connect_target` separate from URL parser | CONNECT has no `://` |
| 010 | `200` to client, silence upstream | curl waits for 200; :443 expects TLS |
| 011 | Mirror backpressure client→upstream | Symmetric tunnel under slow upstream |

---

## 8. File map

```
src/
├── main.c                 # listen, select loop, dispatch
├── include/
│   ├── connection.h       # Connection, ConnState, PendingWriteData
│   ├── proxy.h
│   ├── parser.h
│   ├── handler.h
│   └── error.h
├── proxy/
│   ├── connection.c       # set_nonblock, close_fd
│   └── proxy.c            # handlers, relay, drain
├── http/
│   ├── parser.c
│   └── handler.c
└── error-handling/
    └── error.c
```

---

## 9. Build & verify

```bash
gcc -Wall -Wextra -I src/include \
  src/*.c src/http/*.c src/error-handling/*.c src/proxy/*.c \
  -o server

./server

# HTTP proxy
curl --proxy localhost:8080 http://example.com

# HTTPS tunnel
curl -v --proxy localhost:8080 https://example.com

# Concurrent (M2)
curl --proxy localhost:8080 http://example.com -o /tmp/a.html -w "A %{time_total}\n" &
curl --proxy localhost:8080 http://httpbin.org/delay/5 -o /tmp/b.html -w "B %{time_total}\n" &

# DNS failure
curl --proxy localhost:8080 http://thishostdoesnotexist.invalid/
```

---

## 10. Key mental models

**Byte through HTTP proxy:**
```
curl → proxy: GET http://example.com/foo
proxy → origin: GET /foo + Host
origin → proxy: HTTP response bytes
proxy → curl: same bytes (unparsed)
```

**Byte through HTTPS tunnel:**
```
curl → proxy: CONNECT example.com:443
proxy → curl: 200 Connection Established
curl → proxy: TLS ClientHello (encrypted)
proxy → origin: same bytes (opaque)
origin → proxy: TLS ServerHello (encrypted)
proxy → curl: same bytes (opaque)
... curl and origin do TLS; proxy never decrypts
```

**The fd question (M2 core):**
> Given an arbitrary fd from `select`, which `Connection` slot does it belong to, and what is `current_state`?

Answer: scan `client_array[i]`; match `connection_fd` or `upstream_fd`; dispatch by `current_state`.

---

## 11. Source docs (split originals)

This file merges content from:

- `learning/GLOSSARY.md`, `ARCHITECTURE.md`, `FLOW.md`, `FUNCTIONS.md`, `EDGE-CASES.md`, `DECISIONS.md`
- `milestones/m1-basic-http-forwarding.md`, `m2-event-driven-proxy.md`, `m4-https-via-connect.md`, `m4-todo.md`
- `journal/2026-08-22-m2-s1.md`, `journal/2026-09-12-m4-s1.md`

For session notes or milestone retrospectives, see those paths directly.
