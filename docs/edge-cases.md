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
