# Bacaro — Concepts

## Properties

Everything in Bacaro is a **property** — a named value identified by a dot-separated path of arbitrary depth:

```
sensors.cpu.temperature
network.eth0.rx_bytes
system.battery.level
```

The last segment is the property name; everything before it is the domain path. Property values are [MessagePack](https://msgpack.org/) encoded, so a value can be a scalar (int, float, bool, string), an array, or a map.

## Domains

A **domain** is any prefix of a property path. Subscribing to `sensors` receives updates for `sensors.cpu.temperature`, `sensors.gpu.load`, `sensors.cpu.core0.freq` — any path that starts with `sensors`. Subscribing to `""` (empty string) receives everything.

## Brokerless design

There is no central broker. Each process:

1. Binds a `ZMQ_PUB` socket and a `ZMQ_ROUTER` socket, creating IPC files in the runtime directory.
2. Watches the runtime directory with **inotify** for new peers.
3. Connects a `ZMQ_SUB` socket and a `ZMQ_DEALER` socket to each peer on discovery.
4. Requests a **snapshot** of current state from each peer on connect, seeding its local cache.

After the initial snapshot, live `PUB/SUB` updates keep every subscriber's cache current.

## Last-write-wins

Any process can publish on any path at any time. There is no ownership enforcement. The last published value wins. If the publishing process dies, its last known value persists in every subscriber's cache.

## Publisher identity

No identity frame is added to the wire format. The publisher's name is inferred from which IPC socket the message arrived on — zero overhead.

## Late join / snapshot protocol

When a process starts late (or subscribes to a new domain mid-life), its cache for that domain would be empty until the next published update — which could be never for infrequently changing properties.

Bacaro solves this automatically: every time a new peer is discovered or a new subscription is added, a **snapshot request** is sent to the relevant peer(s) via the `DEALER/ROUTER` socket pair. The peer replies with all matching cached properties in one batch. After that, live `PUB/SUB` takes over.

This means `bacaro_get` always returns the latest known value immediately, without any blocking network call.

## Wire format

Every published message is a three-frame ZMQ multipart message:

```
Frame 0 — topic   : "domain.sub.property"    (ZMQ SUB prefix filter)
Frame 1 — header  : version(1) | flags(1) | sequence(8) | timestamp(8)
Frame 2 — payload : MessagePack bytes
```

Snapshot request/reply uses the same framing over the `DEALER/ROUTER` pair, with a flag byte indicating request, reply, or end-of-snapshot.

For a deeper dive into the internal design, see [architecture.md](architecture.md).
