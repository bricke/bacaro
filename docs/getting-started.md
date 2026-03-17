# Bacaro — Getting Started

## Dependencies

| Dependency | Version | How |
|---|---|---|
| [libzmq](https://zeromq.org/) | 4.x | System package |
| [msgpack-c](https://github.com/msgpack/msgpack-c) | cpp-6.1.1 | CMake `FetchContent`, fetched automatically |
| [doctest](https://github.com/doctest/doctest) | v2.4.11 | CMake `FetchContent`, tests only |

Install system packages:

```sh
sudo apt-get install -y pkg-config libzmq3-dev
```

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Release build:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

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

## Environment variables

| Variable | Default | Description |
|---|---|---|
| `BACARO_RUNTIME_DIR` | `/tmp/bacaro` | Directory where Bacaro creates IPC socket files |
| `BACARO_BUILD_TESTS` | `ON` | Set to `OFF` to skip test compilation |
| `BACARO_BUILD_TOOLS` | `ON` | Set to `OFF` to skip tool compilation (`oste`, `vecio`) |

## Skipping optional components

```sh
# Skip tests (e.g. when embedding as a library)
cmake -B build -DBACARO_BUILD_TESTS=OFF

# Skip tools
cmake -B build -DBACARO_BUILD_TOOLS=OFF

# Both, via environment variables
BACARO_BUILD_TESTS=OFF BACARO_BUILD_TOOLS=OFF cmake -B build
```

## Design constraints

- **Single machine only** — IPC transport (`ipc://`), not TCP
- **No authentication** — all processes on the machine are trusted
- **No persistence** — properties exist only in process memory
- **C++17** — internal implementation; public API is C-compatible (`extern "C"`)
- **Minimal dependencies** — libzmq (system) + msgpack-c (header-only, auto-fetched)
