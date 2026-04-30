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

## bacaro_get returns BACARO_ENOTFOUND immediately after join

**Symptom:** A process calls `bacaro_get` right after `bacaro_new` (or right after `bacaro_subscribe`) and gets `BACARO_ENOTFOUND`, even though the property exists on the bus.

**Cause:** `bacaro_get` is a pure local cache lookup — it never blocks or waits on the network. The snapshot replies from peers are ZMQ messages sitting in a socket buffer; they do not enter the cache until `bacaro_dispatch` is called. Two things must happen before `bacaro_get` returns a value:

1. A snapshot has been requested (requires `bacaro_subscribe`)
2. The snapshot reply has been processed (requires one or more `bacaro_dispatch` calls)

Simply calling `bacaro_new` is not enough — at creation time there are no subscriptions, so no snapshot is requested and the cache stays empty.

**Fix:** Pump the event loop until the property arrives. Two approaches:

**Option 1 — polling loop with sleep.** Simple and portable. The `usleep` prevents spinning at 100% CPU between dispatch calls. Choose the sleep duration based on acceptable latency:

```c
bacaro_t *b = bacaro_new("monitor", NULL);
bacaro_subscribe(b, "sensors");

const uint8_t *out; size_t len;
while (bacaro_get(b, "sensors.temp", &out, &len) == BACARO_ENOTFOUND) {
    bacaro_dispatch(b);
    usleep(1000); // 1ms sleep — tune to taste
}
```

**Option 2 — `poll` on the bacaro fd.** More efficient: the process sleeps in the kernel until there is actual bus activity, then wakes up and dispatches. Avoids both busy-spinning and fixed-interval sleeping:

```c
#include <poll.h>

bacaro_t *b = bacaro_new("monitor", NULL);
bacaro_subscribe(b, "sensors");

const uint8_t *out; size_t len;
struct pollfd pfd = { bacaro_fd(b), POLLIN, 0 };
while (bacaro_get(b, "sensors.temp", &out, &len) == BACARO_ENOTFOUND) {
    poll(&pfd, 1, 100); // sleep until activity, or at most 100ms
    bacaro_dispatch(b);
}
```

In both cases, if the property may never exist, add an iteration cap or a deadline check to avoid an infinite loop.

**Note:** `bacaro_get` never returns invalid or garbage data — only `BACARO_OK` (value present) or `BACARO_ENOTFOUND` (not yet cached).

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

Bacaro uses a **shared SUB socket** for live updates (one socket, O(1) fds regardless of peer count) and **per-peer DEALER sockets** for the snapshot protocol. Per-process fd cost is roughly `3N + 15`, where N is the total number of processes (dominated by per-peer DEALER sockets and their internal ZMQ fds).

**Per-process fd usage and system-wide total:**

| Processes (N) | fds per process | System-wide fds | Assessment |
|---------------|----------------|-----------------|------------|
| 10  | ~45   | ~450    | No tuning needed |
| 50  | ~165  | ~8,250  | No tuning needed |
| 100 | ~315  | ~31,500 | No tuning needed |
| 200 | ~615  | ~123,000 | Comfortable, ulimit tuning recommended |
| 300 | ~915  | ~274,500 | Approaching default `ulimit -n 1024` per process |
| 500 | ~1,515 | ~757,500 | Requires `ulimit -n 4096` |
| 700+ | ~2,115 | ~1,480,000 | Consider a broker or hierarchy |

**Sweet spot:** 50–200 processes. Bacaro performs well with zero tuning in this range.

**Additional constraints at high N:**
- **Snapshot storm on join:** a new process fires N-1 snapshot requests simultaneously; join latency grows as O(N × cache size).
- **Memory:** ~75KB per ZMQ socket × (N-1) DEALER sockets ≈ 15MB per process at N=200.
- **ulimit:** raise `ulimit -n` (or set in `/etc/security/limits.conf`) before crossing ~300 processes.
