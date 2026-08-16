# Forward Proxy Roadmap

Master spec: [`path.txt`](../path.txt)

## Milestones

- [x] **M1 — Basic HTTP Forwarding**
  - DoD: `curl --proxy localhost:8080 http://example.com` (curl → proxy → site → proxy → curl)
  - Keep existing select loop; no epoll yet

- [ ] **M2 — Event-Driven Proxy**
  - DoD: many simultaneous clients; one slow connection does not block others
  - epoll + non-blocking + per-connection state

- [ ] **M3 — Streaming + Backpressure**
  - DoD: slow client or slow upstream without unbounded memory growth

- [ ] **M4 — HTTPS via CONNECT**
  - DoD: `curl --proxy localhost:8080 https://example.com`

- [ ] **M5 — Failure + Resource Management**
  - DoD: no hung connections; no leaks on failure paths

- [ ] **M6 — Operational Software**
  - DoD: config file (listen, timeouts, limits, ports); useful logging

- [ ] **M7 — Validation + Performance**
  - DoD: stress-tested; measured vs direct connection

## Current status

**Active:** M2 — event-driven proxy (epoll + non-blocking + connection state).

**M1 recap:** [`milestones/m1-basic-http-forwarding.md`](milestones/m1-basic-http-forwarding.md)
