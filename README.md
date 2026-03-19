![Bacaro](media/bacaro-banner.png)

[![CI](https://github.com/bricke/bacaro/actions/workflows/ci.yml/badge.svg)](https://github.com/bricke/bacaro/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A reliable, brokerless message bus for single-machine use. Processes publish properties to the bus and subscribe to the domains they care about. Each process maintains a local cache of everything it has received — no central broker, no single point of failure.

Inspired by D-Bus, built on ZeroMQ.

---

## Concepts

### Properties

Everything in Bacaro is a **property** — a named value identified by a dot-separated path of arbitrary depth:

```
sensors.cpu.temperature
network.eth0.rx_bytes
system.battery.level
```

The last segment is the property name; everything before it is the domain path. Property values are [MessagePack](https://msgpack.org/) encoded, so a value can be a scalar (int, float, bool, string), an array, or a map.

### Domains

A **domain** is any prefix of a property path. Subscribing to `sensors` receives updates for `sensors.cpu.temperature`, `sensors.gpu.load`, `sensors.cpu.core0.freq` — any path that starts with `sensors`. Subscribing to `""` (empty string) receives everything.

### Brokerless design

There is no central broker. Each process:

1. Binds a `ZMQ_PUB` socket and a `ZMQ_ROUTER` socket, creating IPC files in the runtime directory.
2. Watches the runtime directory with **inotify** for new peers.
3. Connects a `ZMQ_SUB` socket and a `ZMQ_DEALER` socket to each peer on discovery.
4. Requests a **snapshot** of current state from each peer on connect, seeding its local cache.

After the initial snapshot, live `PUB/SUB` updates keep every subscriber's cache current.

### Last-write-wins

Any process can publish on any path at any time. There is no ownership enforcement. The last published value wins. If the publishing process dies, its last known value persists in every subscriber's cache.

### Publisher identity

No identity frame is added to the wire format. The publisher's name is inferred from which IPC socket the message arrived on — zero overhead.

---

## Wire format

Every published message is a three-frame ZMQ multipart message:

```
Frame 0 — topic   : "domain.sub.property"    (ZMQ SUB prefix filter)
Frame 1 — header  : version(1) | flags(1) | sequence(8) | timestamp(8)
Frame 2 — payload : MessagePack bytes
```

Snapshot request/reply uses the same framing over the `DEALER/ROUTER` pair, with a flag byte indicating request, reply, or end-of-snapshot.

---

## API

```c
/* Lifecycle */
bacaro_t *bacaro_new    (const char *name);
void     bacaro_destroy(bacaro_t **self);

/* Subscriptions */
int bacaro_subscribe     (bacaro_t *self, const char *domain);
int bacaro_subscribe_all (bacaro_t *self);   // subscribe to "" — receives everything
int bacaro_unsubscribe   (bacaro_t *self, const char *domain);

/* Publishing */
int bacaro_set (bacaro_t *self, const char *path,
               const uint8_t *msgpack_value, size_t len);

/* Single property read — immediate cache lookup */
int        bacaro_get          (bacaro_t *self, const char *path,
                                const uint8_t **out, size_t *out_len);
const char *bacaro_get_publisher(bacaro_t *self, const char *path);

/* Batch read — all cached properties matching a domain prefix */
bacaro_proplist_t *bacaro_get_domain      (bacaro_t *self, const char *domain_prefix);
void              bacaro_proplist_destroy(bacaro_proplist_t **list);
size_t            bacaro_proplist_size     (const bacaro_proplist_t *list);
const char       *bacaro_proplist_path     (const bacaro_proplist_t *list, size_t idx);
const uint8_t    *bacaro_proplist_value    (const bacaro_proplist_t *list, size_t idx, size_t *len);
const char       *bacaro_proplist_publisher(const bacaro_proplist_t *list, size_t idx);
uint64_t          bacaro_proplist_sequence (const bacaro_proplist_t *list, size_t idx);
uint64_t          bacaro_proplist_timestamp(const bacaro_proplist_t *list, size_t idx);

/* Event loop integration */
int  bacaro_fd      (bacaro_t *self);   // epoll fd — add to your own event loop
int  bacaro_dispatch(bacaro_t *self);   // call when fd is readable, or periodically

/* Callback fired on every incoming property update */
void bacaro_on_update(bacaro_t *self, bacaro_fn callback, void *userdata);
typedef void (*bacaro_fn)(bacaro_t *self, const char *path,
                         const uint8_t *data, size_t len, void *userdata);

/* Error codes */
// BACARO_OK        0
// BACARO_ENOTFOUND -1
// BACARO_EINVAL    -2
// BACARO_EZMQ      -3
```

### Notes on memory ownership

- `bacaro_get` — `*out` points into internal cache storage. Valid until the next `bacaro_set` or `bacaro_dispatch` on the same path. **Do not free it** — the pointer is not heap-allocated by the caller and calling `free()` on it is undefined behaviour.
- `bacaro_get_publisher` — same rules as above. Do not free.
- `bacaro_get_domain` — returns a heap-allocated list. Caller must call `bacaro_proplist_destroy()`.
- `bacaro_proplist_*` accessors — pointers are valid for the lifetime of the `bacaro_proplist_t`.

---

## Usage example

```c
#include <bacaro.h>
#include <msgpack.h>

// Publisher
bacaro_t *n = bacaro_new("powerd");

msgpack::sbuffer buf;
msgpack::pack(buf, 87.3);   // pack a float with msgpack
bacaro_set(n, "system.battery.level",
          (const uint8_t *)buf.data(), buf.size());

bacaro_destroy(&n);

// Subscriber
static void on_update(bacaro_t *n, const char *path,
                      const uint8_t *data, size_t len, void *ud)
{
    // called on every received update
}

bacaro_t *n = bacaro_new("monitor");
bacaro_subscribe(n, "system");   // subscribe to entire "system" domain
bacaro_on_update(n, on_update, NULL);

// Event loop integration
int epoll_fd = bacaro_fd(n);
// add epoll_fd to your epoll/select, then on activity:
bacaro_dispatch(n);

// Or poll manually:
while (running) {
    bacaro_dispatch(n);
    usleep(1000);
}

// Read from cache at any time
const uint8_t *value;
size_t len;
if (bacaro_get(n, "system.battery.level", &value, &len) == BACARO_OK) {
    double level = msgpack::unpack((const char *)value, len).get().as<double>();
}

// Batch read
bacaro_proplist_t *list = bacaro_get_domain(n, "system");
for (size_t i = 0; i < bacaro_proplist_size(list); ++i) {
    printf("%s published by %s\n",
           bacaro_proplist_path(list, i),
           bacaro_proplist_publisher(list, i));
}
bacaro_proplist_destroy(&list);

bacaro_destroy(&n);
```

---

## Late join / snapshot protocol

When a process starts late (or subscribes to a new domain mid-life), its cache for that domain would be empty until the next published update — which could be never for infrequently changing properties.

Bacaro solves this automatically: every time a new peer is discovered or a new subscription is added, a **snapshot request** is sent to the relevant peer(s) via the `DEALER/ROUTER` socket pair. The peer replies with all matching cached properties in one batch. After that, live `PUB/SUB` takes over.

This means `bacaro_get` always returns the latest known value immediately, without any blocking network call.

---

## Debug / monitoring

### oste

`oste` is a built-in monitoring tool that prints every property update passing through the bus, one per line with a timestamp:

```
[14:32:01.042] sensors.cpu.temperature          = 71.3  (from: powerd)
[14:32:01.043] network.eth0.rx_bytes            = 1048576  (from: netd)
```

Run it directly or via its alias:

```sh
oste
# or
bacaro-monitor
```

`oste` subscribes to all properties and does not publish anything. It exits cleanly on `Ctrl+C`.

### vecio

`vecio` sets a single property from the command line and exits. Useful for injecting values while monitoring with `oste`.

```sh
vecio <path> <value>
# or
bacaro-set <path> <value>
```

Value type is inferred automatically — `true`/`false` become bool, integers become int64, decimals become float64, everything else becomes a string:

```sh
vecio sensors.cpu.temperature 71.3
vecio system.status running
vecio system.reboot_count 5
vecio system.maintenance true
```

To skip building the tools:

```sh
cmake -B build -DBACARO_BUILD_TOOLS=OFF
# or via environment variable:
BACARO_BUILD_TOOLS=OFF cmake -B build
```

---

## Dependencies

| Dependency | Version | How |
|---|---|---|
| [libzmq](https://zeromq.org/) | 4.x | System package |
| [msgpack-c](https://github.com/msgpack/msgpack-c) | cpp-6.1.1 | CMake `FetchContent`, no install needed |
| [doctest](https://github.com/doctest/doctest) | v2.4.11 | CMake `FetchContent`, tests only |

### Install system packages

```sh
sudo apt-get install -y pkg-config libzmq3-dev
```

---

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Release build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Skip tests (e.g. when installing as a library)

```sh
cmake -B build -DBACARO_BUILD_TESTS=OFF
# or via environment variable:
BACARO_BUILD_TESTS=OFF cmake -B build
```

---

## Tests

```sh
ctest --test-dir build --output-on-failure
```

| Suite | What it covers |
|---|---|
| `test_wire` | Frame encode/decode, roundtrip, bad frames, timestamps |
| `test_cache` | Set/get, last-write-wins, prefix matching, size/clear |
| `test_lifecycle` | IPC file creation/removal, null args, `BACARO_RUNTIME_DIR` |
| `test_discovery` | Peer discovery, epoll fd, disconnect, subscribe/unsubscribe |
| `test_snapshot` | Late join, mid-life subscribe, `subscribe_all` full snapshot |
| `test_pubsub` | Publish/subscribe, domain filtering, callbacks, dead publisher cache persistence |
| `test_get_domain` | Batch read, all accessors, null safety, partial segment matching |

---

## Environment

| Variable | Default | Description |
|---|---|---|
| `BACARO_RUNTIME_DIR` | `/tmp/bacaro` | Directory where Bacaro creates IPC socket files |
| `BACARO_BUILD_TESTS` | `ON` | Set to `OFF` to skip test compilation |
| `BACARO_BUILD_TOOLS` | `ON` | Set to `OFF` to skip tool compilation (`oste`) |

---

## Design constraints

- **Single machine only** — IPC transport (`ipc://`), not TCP
- **Single-threaded** — all calls on a `bacaro_t` instance must happen from one thread (the event-loop thread). ZMQ sockets are not safe for concurrent use, and `bacaro_get` returns interior pointers invalidated by the next `set`/`dispatch`. If you need multi-threaded access, protect the instance with an external mutex.
- **Linux only** — uses epoll and inotify for event dispatch and peer discovery
- **No authentication** — all processes on the machine are trusted
- **No persistence** — properties exist only in process memory; a full bus restart clears all state
- **C++17** — internal implementation; public API is C-compatible (`extern "C"`)
- **Minimal dependencies** — libzmq + msgpack-c (header-only, auto-fetched)
