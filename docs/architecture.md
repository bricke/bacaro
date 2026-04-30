# Bacaro — Architecture

This document describes the internal design of Bacaro for contributors and AI agents working on the codebase. For the public API, see the [README](../README.md) and [`include/bacaro.h`](../include/bacaro.h).

---

## Overview

Bacaro is a fully brokerless mesh. Every process is both a publisher and a potential snapshot server. There is no privileged node — any process can come and go at any time.

```
Process A                    Process B
─────────────────────        ─────────────────────
PUB ──ipc──────────────────► shared SUB
ROUTER ◄──ipc──────────────── DEALER  (snapshot)

shared SUB ◄──ipc──────────── PUB
DEALER ──ipc────────────────► ROUTER  (snapshot)
```

Each process uses a **single shared SUB socket** that connects to every peer's PUB endpoint (via `zmq_connect`/`zmq_disconnect`). This keeps live-update fd usage at O(1) regardless of peer count. Per-peer **DEALER sockets** are still used for the snapshot protocol, since ZMQ DEALER round-robins outgoing messages.

---

## Lifecycle

### Startup (`bacaro_new`)

1. Generate a UUID for this instance.
2. Resolve the runtime directory (`BACARO_RUNTIME_DIR`, default `/tmp/bacaro`).
3. Create the runtime directory if it does not exist.
4. If `published_domains` is non-NULL, write a `.manifest` file (`<name>.<uuid>.manifest`) listing one domain per line — written **before** discovery so peers see it when they find the `.pub` file.
5. Bind a `ZMQ_PUB` socket → IPC file `<name>.<uuid>.pub`
6. Bind a `ZMQ_ROUTER` socket → IPC file `<name>.<uuid>.rep`
7. Call `discovery_init` (see Discovery below).

The UUID in the filename prevents collisions when a process restarts quickly under the same name.

### Shutdown (`bacaro_destroy`)

1. `discovery_cleanup`: disconnect all peers, close epoll and inotify fds.
2. Set `ZMQ_LINGER = 200ms` on the PUB socket before closing, so any recently published messages have time to flush to connected peers (see [Slow-Joiner Note](#slow-joiner-note)).
3. Close and remove the `.pub`, `.rep`, and `.manifest` IPC files.
4. Destroy the ZMQ context.

---

## Discovery

**File:** `src/discovery.cpp`

Bacaro uses the filesystem as a service directory and inotify for change notifications.

### Initialisation (`discovery_init`)

1. Create an `epoll` instance (`EPOLL_CLOEXEC`).
2. Create the **shared SUB socket** and register its fd with epoll.
3. Create an `inotify` instance (`IN_NONBLOCK | IN_CLOEXEC`).
4. Add an inotify watch on the runtime directory for `IN_CREATE | IN_DELETE`.
5. Register the inotify fd with epoll.
6. Register the PUB and ROUTER sockets with epoll (via their ZMQ file descriptors).
7. **Scan the runtime directory** for existing `.pub` files and connect to each peer found.

inotify is set up **before** the directory scan to close the race window where a peer could appear between the scan and the watch being established.

### Peer connect (`discovery_peer_connect`)

Triggered by a new `.pub` file appearing (either from the initial scan or from an inotify `IN_CREATE` event).

1. Extract the peer name from the filename (everything before the first `.`).
2. Derive the `.rep` filename by replacing the `.pub` suffix.
3. Call `zmq_connect` on the **shared SUB socket** to the peer's PUB endpoint.
4. Read the peer's `.manifest` file if present; store declared domains and `has_manifest` flag in `PeerInfo`.
5. Record the peer in the `peers` map, storing both the PUB and REP endpoints.
6. **Lazy DEALER**: if the process has active subscriptions, iterate them. For each prefix that overlaps with the peer's manifest (or if the peer has no manifest), create a per-peer `ZMQ_DEALER` socket, connect to the REP endpoint, register its fd with epoll, and send a snapshot request. Peers with a manifest that doesn't overlap any active subscription get no DEALER socket at all.

### Peer disconnect (`discovery_peer_disconnect`)

Triggered by an inotify `IN_DELETE` event for a `.pub` file.

1. Call `zmq_disconnect` on the shared SUB socket for the peer's PUB endpoint.
2. Remove the DEALER fd from epoll and close the DEALER socket.
3. Remove the peer from all maps.

**Cache entries are intentionally preserved** — the last known values from a dead peer remain available until overwritten.

---

## Subscriptions

**File:** `src/bacaro.cpp`

The process maintains a `std::vector<std::string> subscriptions` list of domain prefixes.

- `bacaro_subscribe(domain)`: adds the prefix, calls `zmq_setsockopt(ZMQ_SUBSCRIBE)` on the shared SUB socket, and sends a snapshot request to every existing peer's DEALER socket for the new domain.
- `bacaro_subscribe_all()`: calls `bacaro_subscribe("")` — an empty ZMQ subscription prefix matches everything.
- `bacaro_unsubscribe(domain)`: removes the prefix and calls `zmq_setsockopt(ZMQ_UNSUBSCRIBE)` on the shared SUB socket.

Subscriptions are per-socket, not per-connection. New `zmq_connect` calls on the shared SUB socket automatically inherit existing subscription filters.

---

## Publishing

**File:** `src/bacaro.cpp`

`bacaro_set(path, msgpack_value, len)`:

1. Increment the per-process monotonic sequence counter.
2. Record the current timestamp (`CLOCK_REALTIME` via `wire_now_us()`).
3. Update the **local cache** first (so a late `bacaro_get` after `bacaro_set` returns the new value immediately).
4. Pack the message into a three-frame ZMQ multipart and send on the PUB socket.

---

## Wire Format

**File:** `src/wire.h`, `src/wire.cpp`

Every published message is a four-frame ZMQ multipart (wire version 2):

```
Frame 0 — topic     : UTF-8 path string, used as ZMQ SUB prefix filter
Frame 1 — publisher : UTF-8 publisher name (carried on the wire for shared SUB)
Frame 2 — header    : 18 bytes, packed struct
                      version(1) | flags(1) | sequence(8) | timestamp(8)
Frame 3 — payload   : raw MessagePack bytes
```

`WireHeader` is a packed struct (`#pragma pack(1)`). Sequence is a per-publisher monotonic counter; timestamp is microseconds since the Unix epoch.

Flag values:

| Flag | Value | Meaning |
|------|-------|---------|
| `BACARO_FLAG_NONE`         | 0x00 | Normal published message |
| `BACARO_FLAG_SNAPSHOT_REQ` | 0x01 | Snapshot request (DEALER→ROUTER) |
| `BACARO_FLAG_SNAPSHOT_REP` | 0x02 | Snapshot reply entry (ROUTER→DEALER) |
| `BACARO_FLAG_SNAPSHOT_END` | 0x03 | End of snapshot (ROUTER→DEALER) |

---

## Snapshot Protocol

**File:** `src/wire.cpp`

The snapshot protocol solves the late-join problem: a process that subscribes to a domain after properties have already been published would otherwise miss them.

### Request (DEALER → ROUTER)

```
Frame 0 — flag byte : BACARO_FLAG_SNAPSHOT_REQ
Frame 1 — prefix    : domain prefix string (empty = all)
```

### Reply (ROUTER → DEALER), one message per matching entry

```
Frame 0 — identity  : ZMQ ROUTER identity frame
Frame 1 — flag byte : BACARO_FLAG_SNAPSHOT_REP
Frame 2 — topic     : property path
Frame 3 — publisher : original publisher name
Frame 4 — header    : WireHeader (version, flags, sequence, timestamp)
Frame 5 — payload   : MessagePack bytes
```

### End marker

```
Frame 0 — identity  : ZMQ ROUTER identity frame
Frame 1 — flag byte : BACARO_FLAG_SNAPSHOT_END
```

The END marker is always sent, even if a send error occurs mid-snapshot, so the receiving peer is never left blocking indefinitely.

Snapshot requests are sent:
- When a new peer is discovered (for all currently subscribed domains).
- When `bacaro_subscribe` is called (for all currently connected peers).

---

## Dispatch Loop

**File:** `src/bacaro.cpp`

`bacaro_dispatch(self)` is the engine of Bacaro. It calls `epoll_wait` with timeout 0 (non-blocking) and processes all ready file descriptors:

| fd | Source | Action |
|----|--------|--------|
| `inotify_fd` | Peer arrived / left | `discovery_process_inotify` |
| `router_fd` | Incoming snapshot request | `snapshot_handle_request` |
| per-peer DEALER fd | Incoming snapshot reply | `snapshot_recv_one` → `apply_message` |
| `sub_fd` (shared) | Live published messages from all peers | `wire_recv` + `wire_unpack` → `apply_message` |

`apply_message` updates the local cache and fires the `on_update` callback if registered.

`drain_zmq` is used for ZMQ sockets: it loops calling the handler until `ZMQ_EVENTS` no longer has `ZMQ_POLLIN`, because a single epoll readiness event can correspond to multiple pending ZMQ messages.

### Event loop integration

`bacaro_fd()` returns the epoll fd. Callers can add it to their own `epoll`/`poll`/`select` loop and call `bacaro_dispatch` when it becomes readable. Alternatively, `bacaro_dispatch` can be called periodically (e.g. every 10ms) without epoll integration.

---

## Local Cache

**File:** `src/cache.h`, `src/cache.cpp`

```
std::unordered_map<std::string, CacheEntry>
```

Each `CacheEntry` holds:
- `payload` — the raw MessagePack bytes (`std::vector<uint8_t>`)
- `publisher` — name of the last process to set this property
- `sequence` — publisher's monotonic counter at time of publish
- `timestamp` — microseconds since epoch at time of publish

**Last-write-wins** with convergence guarantee: multiple processes can write to the same path (e.g. a battery monitor, light sensor, and user process all adjusting `display.backlight`). `Cache::set` discards stale updates — the entry with the highest timestamp wins. On equal timestamps, the lexicographically higher publisher name is used as a deterministic tiebreaker, ensuring all nodes converge to the same value regardless of message arrival order.

**Prefix matching** in `get_prefix(prefix)`:
- Empty prefix matches everything.
- Otherwise, a path matches if it equals the prefix exactly, or starts with `prefix + "."` — the dot check prevents `sensors.cpu` from matching `sensors.cpu_fan` (partial segment safety).

---

## Publisher Identity

Publisher identity is carried explicitly in the wire format as frame 1 of every published message and frame 3 of every snapshot reply. This allows the use of a single shared SUB socket for all peers — since all messages arrive on the same socket, the publisher must be identified from the message itself rather than from which socket delivered it.

---

## Slow-Joiner Note

ZMQ PUB/SUB has a known "slow-joiner" problem: when a SUB socket connects to a PUB socket, there is a brief window during which subscription filters have not yet propagated. Messages published in this window are silently dropped. Bacaro mitigates this through the snapshot protocol — late joiners always request a full snapshot on connect, so the current state is always recoverable regardless of timing. The `ZMQ_LINGER = 200ms` on the PUB socket at shutdown additionally ensures recently published messages have time to flush before the process exits.

---

## Internal Structs

**File:** `src/internal.h`

### `bacaro_s`

The main handle. One per process. Contains:
- ZMQ context and bound sockets (`pub_sock`, `router_sock`)
- `sub_sock` — shared ZMQ_SUB socket (one per process, connects to all peers)
- `own_pub` — this process's `.pub` filename, used to ignore our own inotify events
- `peers` map: filename → `PeerInfo`
- `dealer_fd_to_filename` — reverse map for per-peer DEALER dispatch
- `cache` — the local property cache
- `sequence` — monotonic publish counter
- `subscriptions` — list of subscribed domain prefixes
- `inotify_fd`, `inotify_wd`, `epoll_fd`, `sub_fd`, `router_fd`
- `on_update_cb` / `on_update_data` — user callback

### `PeerInfo`

Per-peer connection state:
- `dealer_sock` — per-peer ZMQ DEALER socket for snapshot protocol (created lazily, may be `nullptr`)
- `name` — peer process name (extracted from filename)
- `pub_endpoint` — stored for `zmq_disconnect` on the shared SUB socket
- `rep_endpoint` — stored for lazy DEALER connect
- `dealer_fd` — ZMQ fd registered with epoll (`-1` until DEALER is created)
- `manifest` — declared published domains read from the peer's `.manifest` file
- `has_manifest` — `true` if a `.manifest` file was found at discovery time
