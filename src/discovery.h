#pragma once

#include "internal.h"

// Set up inotify, epoll, scan for existing peers and connect to them.
// Called from bacaro_new() after ZMQ sockets are bound.
int discovery_init(bacaro_t *self);

// Close inotify, epoll, disconnect all peers.
// Called from bacaro_destroy().
void discovery_cleanup(bacaro_t *self);

// Apply all current subscriptions to a single SUB socket.
// Called when a new peer is connected or a new subscription is added.
void discovery_apply_subscriptions(bacaro_t *self, void *sub_sock);

// Connect a SUB socket to a peer's PUB file (filename only, not full path).
// No-op if already connected.
int discovery_peer_connect(bacaro_t *self, const std::string &filename);

// Disconnect from a peer's PUB file. Cache entries are preserved.
void discovery_peer_disconnect(bacaro_t *self, const std::string &filename);

// Read and process pending inotify events (peer joins/leaves).
// Called from bacaro_dispatch() when inotify_fd is readable.
void discovery_process_inotify(bacaro_t *self);

// Register a ZMQ socket's ZMQ_FD with the epoll fd.
void discovery_epoll_add_zmq(bacaro_t *self, void *sock);

// Unregister a ZMQ socket's ZMQ_FD from the epoll fd.
void discovery_epoll_remove_zmq(bacaro_t *self, void *sock);
