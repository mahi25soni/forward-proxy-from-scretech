# Architecture Decision Records

Short ADRs — ~5 lines each. Add when we commit to an approach.

## ADR-001: Keep select() for M1
**Decision:** Reuse existing select loop; handle upstream inline in `handle_client`.
**Why:** path.txt allows deferring epoll; smallest path to working forward proxy.
**Revisit:** M2 (event-driven proxy).

## ADR-002: Upstream fd outside select for M1
**Decision:** Blocking connect/write/read relay in `handle_client`; do not add upstream to `read_fds`.
**Why:** One synchronous transaction per request; no connection state machine yet.
**Revisit:** M2 (both fds in epoll with per-connection state).
