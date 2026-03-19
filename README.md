![Bacaro](media/bacaro-banner.png)

<div align="center">

[![CI (GCC)](https://github.com/bricke/bacaro/actions/workflows/ci.yml/badge.svg)](https://github.com/bricke/bacaro/actions/workflows/ci.yml)
[![CI (Clang)](https://github.com/bricke/bacaro/actions/workflows/ci-clang.yml/badge.svg)](https://github.com/bricke/bacaro/actions/workflows/ci-clang.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

</div>

A reliable, brokerless message bus for single-machine use. Processes publish named properties and subscribe to the domains they care about. Each process maintains a local cache — no central broker, no single point of failure.

Inspired by D-Bus, built on ZeroMQ.

## Quick start

```sh
sudo apt-get install -y pkg-config libzmq3-dev
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Documentation

| Document | Description |
|---|---|
| [Concepts](docs/concepts.md) | Properties, domains, brokerless design, late-join, wire format |
| [API Reference](docs/api.md) | Full API, memory ownership, usage example |
| [Getting Started](docs/getting-started.md) | Dependencies, build options, tests, environment variables |
| [Tools](docs/tools.md) | `oste` (monitor) and `vecio` (property setter) |
| [Architecture](docs/architecture.md) | Internal design: discovery, dispatch, snapshot protocol, cache |
| [Edge Cases](docs/edge-cases.md) | Known gotchas, limitations, and how to work around them |

## Dependencies

| Dependency | Version | How |
|---|---|---|
| [libzmq](https://zeromq.org/) | 4.x | System package |
| [msgpack-c](https://github.com/msgpack/msgpack-c) | cpp-6.1.1 | CMake `FetchContent` |
| [doctest](https://github.com/doctest/doctest) | v2.4.11 | CMake `FetchContent`, tests only |

## Design constraints

- **Single machine only** — IPC transport (`ipc://`), not TCP
- **Single-threaded** — all calls on a `bacaro_t` instance must happen from one thread (the event-loop thread). ZMQ sockets are not safe for concurrent use, and `bacaro_get` returns interior pointers invalidated by the next `set`/`dispatch`. If you need multi-threaded access, protect the instance with an external mutex.
- **Linux only** — uses epoll and inotify for event dispatch and peer discovery
- **No authentication** — all processes on the machine are trusted
- **No persistence** — properties exist only in process memory; a full bus restart clears all state

## License

[MIT](LICENSE) — Copyright 2026 Matteo Brichese
