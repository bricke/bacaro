# Bacaro — Edge Cases

This document covers known edge cases, limitations, and gotchas.

---

## vecio: message not received by subscribers

**Symptom:** `oste` doesn't show a property set by `vecio`.

**Cause:** ZMQ PUB/SUB has a slow-joiner problem. When `vecio` starts, it creates its `.pub` IPC file and publishes immediately. Subscribers watching via inotify need a moment to see the new file, connect their SUB socket, and propagate their subscription filter. If `vecio` publishes and exits before that happens, the message is dropped silently.

**Fix:** Use the `-w` flag to wait before publishing:

```sh
vecio -w 500 sensors.cpu.temperature 71.3
```

500ms is usually enough. If `oste` is already running and the bus is established, a shorter value (100–200ms) may suffice.

---

## vecio: not designed for high-frequency bulk publishing

**Symptom:** Running `vecio` in a tight loop hundreds of times causes `oste` to crash with "Too many open files".

**Cause:** Every `vecio` invocation is a full peer. `oste` attempts to open two sockets (SUB + DEALER) per peer. Running hundreds of short-lived processes faster than `oste` can process inotify cleanup events exhausts the process file descriptor limit (~1024 by default), causing ZMQ to abort internally.

**Fix:** For high-frequency or bulk publishing, use a single long-lived process that calls `bacaro_set` in a loop — not a new `vecio` process each time. `vecio` is a manual debugging tool, not a batch injector.

---

## Dead publisher persistence

**Symptom:** A property last set by a process that has since exited is still visible via `bacaro_get` and `oste`.

**Cause:** This is by design. Bacaro caches the last known value of every property in each subscriber's local cache. When a publisher exits, its IPC files are removed and peers disconnect, but cached values are intentionally preserved. The `publisher` field returned by `bacaro_get_publisher` will still show the original process name.

**Note:** When the original publisher restarts (under the same name, but a new UUID), it starts with an empty cache. Other processes still hold the old values and will continue publishing them via snapshot if asked.

---

## Late-joining subscriber misses no properties

**Symptom (expected):** A process that subscribes mid-session sees properties published long before it joined.

**Cause:** This is by design. On every new peer connection or new `bacaro_subscribe` call, a snapshot request is sent to all relevant peers. They reply with their entire cached state for that domain. `bacaro_get` will return valid data immediately after the snapshot completes, even for properties that were last published before the process started.

---

## Two processes with the same name

Each Bacaro instance generates a UUID at startup appended to its IPC filename (`<name>.<uuid>.pub`). Two processes with the same name are treated as separate peers — they will both publish and both be discovered. Properties published by either will coexist in the cache, with last-write-wins applying per path.

---

## Runtime directory on a network filesystem

inotify does not work on NFS or other network filesystems. `BACARO_RUNTIME_DIR` must point to a local filesystem (tmpfs, ext4, etc.). The default `/tmp/bacaro` is always local.

---

## No property size limit

Bacaro does not enforce a maximum property value size. Very large MessagePack payloads will be held in memory by every subscriber and transmitted in full during every snapshot. Keep property values small — they are meant to represent state (a temperature, a status string, a counter), not bulk data.

---

## Scalability: process count vs. file descriptor usage

Bacaro is a fully connected mesh — every process holds two ZMQ sockets per peer (SUB + DEALER). ZMQ uses approximately 3 file descriptors per socket internally. Per-process fd cost is roughly `6N`, where N is the total number of processes.

**Per-process fd usage and system-wide total:**

| Processes (N) | fds per process | System-wide fds | Assessment |
|---------------|----------------|-----------------|------------|
| 10  | ~60   | ~600    | No tuning needed |
| 50  | ~300  | ~15,000 | No tuning needed |
| 100 | ~600  | ~60,000 | Comfortable, ulimit tuning recommended |
| 150 | ~900  | ~135,000 | Approaching default `ulimit -n 1024` per process |
| 200 | ~1,200 | ~240,000 | Exceeds default per-process limit — requires `ulimit -n 4096` |
| 300 | ~1,800 | ~540,000 | Possible with tuning; snapshot join latency becomes significant |
| 500+ | ~3,000 | ~1,500,000 | O(N²) mesh is the architectural bottleneck — consider a broker |

**Sweet spot:** 50–100 processes. Bacaro performs well with zero tuning in this range, which covers typical single-machine topologies (system daemons, per-subsystem services).

**Additional constraints at high N:**
- **Snapshot storm on join:** a new process fires N-1 snapshot requests simultaneously; join latency grows as O(N × cache size).
- **Memory:** ~75KB per ZMQ socket × 2(N-1) sockets ≈ 30MB per process at N=200.
- **ulimit:** raise `ulimit -n` (or set in `/etc/security/limits.conf`) before crossing ~150 processes.
