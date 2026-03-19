#include "internal.h"
#include "discovery.h"
#include "wire.h"

#include <zmq.h>
#include <sys/epoll.h>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string generate_uuid()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(16) << dis(gen)
       << std::setw(16) << dis(gen);
    return ss.str();
}

static std::string resolve_runtime_dir()
{
    const char *env = std::getenv("BACARO_RUNTIME_DIR");
    return env ? std::string(env) : "/tmp/bacaro";
}

static std::string ipc_path(bacaro_t *self, const char *suffix)
{
    return self->runtime_dir + "/" + self->name + "." + self->uuid + suffix;
}

static int bind_ipc(void *sock, const std::string &path)
{
    std::string endpoint = "ipc://" + path;
    return zmq_bind(sock, endpoint.c_str()) == 0 ? BACARO_OK : BACARO_EZMQ;
}

static void apply_message(bacaro_t *self, const WireMessage &msg, const std::string &peer_name)
{
    self->cache.set(msg.topic, msg.payload, peer_name, msg.header.sequence, msg.header.timestamp);
    if (self->on_update_cb)
        self->on_update_cb(self, msg.topic.c_str(),
                           msg.payload.data(), msg.payload.size(),
                           self->on_update_data);
}

// Look up a peer by ZMQ fd using the given fd→filename reverse map
static PeerInfo *find_peer(bacaro_t *self,
                           const std::unordered_map<int, std::string> &fd_map, int fd)
{
    auto fit = fd_map.find(fd);
    if (fit == fd_map.end()) return nullptr;
    auto pit = self->peers.find(fit->second);
    return pit != self->peers.end() ? &pit->second : nullptr;
}

// Drain all pending ZMQ messages from a socket, calling handler for each
static void drain_zmq(void *sock, const std::function<void(void *)> &handler)
{
    while (true) {
        uint32_t events = 0;
        size_t sz = sizeof(events);
        zmq_getsockopt(sock, ZMQ_EVENTS, &events, &sz);
        if (!(events & ZMQ_POLLIN))
            break;
        handler(sock);
    }
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

bacaro_t *bacaro_new(const char *name)
{
    if (!name || name[0] == '\0')
        return nullptr;

    auto *self = new (std::nothrow) bacaro_s();
    if (!self)
        return nullptr;

    self->name        = name;
    self->uuid        = generate_uuid();
    self->runtime_dir = resolve_runtime_dir();
    self->own_pub     = self->name + "." + self->uuid + ".pub";

    std::error_code ec;
    fs::create_directories(self->runtime_dir, ec);
    if (ec)
        goto fail;

    self->zmq_ctx = zmq_ctx_new();
    if (!self->zmq_ctx)
        goto fail;

    self->pub_sock = zmq_socket(self->zmq_ctx, ZMQ_PUB);
    if (!self->pub_sock)
        goto fail;

    if (bind_ipc(self->pub_sock, ipc_path(self, ".pub")) != BACARO_OK)
        goto fail;

    self->router_sock = zmq_socket(self->zmq_ctx, ZMQ_ROUTER);
    if (!self->router_sock)
        goto fail;

    if (bind_ipc(self->router_sock, ipc_path(self, ".rep")) != BACARO_OK)
        goto fail;

    if (discovery_init(self) != BACARO_OK)
        goto fail;

    return self;

fail:
    bacaro_destroy(&self);
    return nullptr;
}

void bacaro_destroy(bacaro_t **self_ptr)
{
    if (!self_ptr || !*self_ptr)
        return;

    bacaro_t *self = *self_ptr;

    discovery_cleanup(self);

    auto close_and_remove = [&](void *&sock, const char *suffix) {
        if (!sock) return;
        zmq_close(sock);
        sock = nullptr;
        std::error_code ec;
        fs::remove(ipc_path(self, suffix), ec);
    };

    // ZMQ PUB/SUB has a "slow-joiner" problem: on process startup, connections
    // between sockets take a moment to establish and subscriptions to propagate.
    // If a message is published and the process exits before ZMQ has flushed it
    // to connected subscribers, the message is silently dropped. Setting
    // ZMQ_LINGER to 200ms on the PUB socket gives ZMQ enough time to deliver
    // recently published messages to already-connected peers before teardown.
    // 200ms is chosen as a balance between reliability and acceptable exit latency.
    if (self->pub_sock) {
        int linger_ms = 200;
        zmq_setsockopt(self->pub_sock, ZMQ_LINGER, &linger_ms, sizeof(linger_ms));
    }
    close_and_remove(self->pub_sock,    ".pub");
    close_and_remove(self->router_sock, ".rep");

    if (self->zmq_ctx) {
        zmq_ctx_destroy(self->zmq_ctx);
        self->zmq_ctx = nullptr;
    }

    delete self;
    *self_ptr = nullptr;
}

// ── Subscriptions ─────────────────────────────────────────────────────────────

int bacaro_subscribe(bacaro_t *self, const char *domain)
{
    if (!self || !domain)
        return BACARO_EINVAL;

    std::string prefix(domain);

    for (const auto &s : self->subscriptions)
        if (s == prefix) return BACARO_OK;

    self->subscriptions.push_back(prefix);

    for (auto &[filename, peer] : self->peers) {
        zmq_setsockopt(peer.sub_sock, ZMQ_SUBSCRIBE, prefix.c_str(), prefix.size());
        snapshot_send_request(peer.dealer_sock, prefix);
    }

    return BACARO_OK;
}

int bacaro_subscribe_all(bacaro_t *self)
{
    return bacaro_subscribe(self, "");
}

int bacaro_unsubscribe(bacaro_t *self, const char *domain)
{
    if (!self || !domain)
        return BACARO_EINVAL;

    std::string prefix(domain);
    auto &subs = self->subscriptions;
    subs.erase(std::remove(subs.begin(), subs.end(), prefix), subs.end());

    for (auto &[filename, peer] : self->peers)
        zmq_setsockopt(peer.sub_sock, ZMQ_UNSUBSCRIBE, prefix.c_str(), prefix.size());

    return BACARO_OK;
}

// ── Publishing ────────────────────────────────────────────────────────────────

int bacaro_set(bacaro_t *self, const char *path,
              const uint8_t *msgpack_value, size_t len)
{
    if (!self || !path || !msgpack_value)
        return BACARO_EINVAL;

    uint64_t seq = ++self->sequence;
    uint64_t ts  = wire_now_us();
    Frame    payload(msgpack_value, msgpack_value + len);

    // Update local cache first
    self->cache.set(path, payload, self->name, seq, ts);

    // Broadcast to all subscribers
    WireMessage msg;
    msg.topic   = path;
    msg.header  = { BACARO_WIRE_VERSION, BACARO_FLAG_NONE, seq, ts };
    msg.payload = std::move(payload);

    return wire_send(self->pub_sock, wire_pack(msg));
}

// ── Reading ───────────────────────────────────────────────────────────────────

int bacaro_get(bacaro_t *self, const char *path,
              const uint8_t **out, size_t *out_len)
{
    if (!self || !path || !out || !out_len)
        return BACARO_EINVAL;

    const CacheEntry *entry = self->cache.get(path);
    if (!entry)
        return BACARO_ENOTFOUND;

    *out     = entry->payload.data();
    *out_len = entry->payload.size();
    return BACARO_OK;
}

const char *bacaro_get_publisher(bacaro_t *self, const char *path)
{
    if (!self || !path)
        return nullptr;

    const CacheEntry *entry = self->cache.get(path);
    if (!entry)
        return nullptr;

    return entry->publisher.c_str();
}

// ── Event loop ────────────────────────────────────────────────────────────────

int bacaro_fd(bacaro_t *self)
{
    if (!self) return -1;
    return self->epoll_fd;
}

int bacaro_dispatch(bacaro_t *self)
{
    if (!self) return BACARO_EINVAL;

    struct epoll_event events[32];
    int n = epoll_wait(self->epoll_fd, events, 32, 0);

    for (int i = 0; i < n; ++i) {
        int fd = events[i].data.fd;

        // ── inotify: peer join / leave ────────────────────────────────────
        if (fd == self->inotify_fd) {
            discovery_process_inotify(self);
            continue;
        }

        // ── ROUTER: incoming snapshot request ────────────────────────────
        if (fd == self->router_fd) {
            drain_zmq(self->router_sock, [&](void *sock) {
                snapshot_handle_request(sock, self->cache);
            });
            continue;
        }

        // ── DEALER: incoming snapshot reply ──────────────────────────────
        if (auto *peer = find_peer(self, self->dealer_fd_to_filename, fd)) {
            drain_zmq(peer->dealer_sock, [&](void *sock) {
                WireMessage msg;
                bool is_end = false;
                if (snapshot_recv_one(sock, msg, is_end) != BACARO_OK || is_end)
                    return;
                apply_message(self, msg, peer->name);
            });
            continue;
        }

        // ── SUB: live published message from a peer ───────────────────────
        if (auto *peer = find_peer(self, self->sub_fd_to_filename, fd)) {
            drain_zmq(peer->sub_sock, [&](void *sock) {
                Frames frames;
                if (wire_recv(sock, frames) != BACARO_OK)
                    return;
                WireMessage msg;
                if (wire_unpack(frames, msg) != BACARO_OK)
                    return;
                apply_message(self, msg, peer->name);
            });
        }
    }

    return BACARO_OK;
}

void bacaro_on_update(bacaro_t *self, bacaro_fn callback, void *userdata)
{
    if (!self) return;
    self->on_update_cb   = callback;
    self->on_update_data = userdata;
}

// ── Batch read ────────────────────────────────────────────────────────────────

bacaro_proplist_t *bacaro_get_domain(bacaro_t *self, const char *domain_prefix)
{
    if (!self || !domain_prefix)
        return nullptr;

    auto *list = new (std::nothrow) bacaro_proplist_s();
    if (!list)
        return nullptr;

    list->entries = self->cache.get_prefix(domain_prefix);
    return list;
}

void bacaro_proplist_destroy(bacaro_proplist_t **list)
{
    if (!list || !*list)
        return;
    delete *list;
    *list = nullptr;
}

size_t bacaro_proplist_size(const bacaro_proplist_t *list)
{
    if (!list) return 0;
    return list->entries.size();
}

static inline bool valid_idx(const bacaro_proplist_t *list, size_t idx)
{
    return list && idx < list->entries.size();
}

const char *bacaro_proplist_path(const bacaro_proplist_t *list, size_t idx)
{
    if (!valid_idx(list, idx)) return nullptr;
    return list->entries[idx].first.c_str();
}

const uint8_t *bacaro_proplist_value(const bacaro_proplist_t *list, size_t idx, size_t *len)
{
    if (!valid_idx(list, idx) || !len) return nullptr;
    const auto &entry = list->entries[idx].second;
    *len = entry.payload.size();
    return entry.payload.data();
}

const char *bacaro_proplist_publisher(const bacaro_proplist_t *list, size_t idx)
{
    if (!valid_idx(list, idx)) return nullptr;
    return list->entries[idx].second.publisher.c_str();
}

uint64_t bacaro_proplist_sequence(const bacaro_proplist_t *list, size_t idx)
{
    if (!valid_idx(list, idx)) return 0;
    return list->entries[idx].second.sequence;
}

uint64_t bacaro_proplist_timestamp(const bacaro_proplist_t *list, size_t idx)
{
    if (!valid_idx(list, idx)) return 0;
    return list->entries[idx].second.timestamp;
}
