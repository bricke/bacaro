# Changelog

All notable changes to this project will be documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/).

## [0.1.0] — 2026-03-18

Initial public release.

### Added
- Core library: brokerless pub/sub over ZeroMQ IPC with automatic peer discovery via inotify
- Properties with dot-separated paths, domain-based subscriptions, local cache per process
- Snapshot protocol for late-joining subscribers
- C-compatible public API (`bacaro.h`) with 17 functions
- `oste` monitoring tool (prints all property updates with timestamps)
- `vecio` CLI tool (sets a single property and exits, with optional `-w` delay)
- 7 test suites covering wire format, cache, lifecycle, discovery, snapshot, pub/sub, batch read
- CI via GitHub Actions (Debug + Release, Ubuntu)

### Design constraints
- Single machine only (IPC transport)
- Single-threaded — all API calls must happen from one thread per `bacaro_t` instance
- No authentication — all processes on the machine are trusted
- No persistence — properties exist only in process memory
- Linux only (epoll, inotify)
