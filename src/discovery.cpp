#include "discovery.h"
#include "wire.h"

#include <sys/epoll.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;

static bool ends_with(const std::string &s, const std::string &suffix)
{
    return s.size() >= suffix.size()
        && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static int get_zmq_fd(void *sock)
{
    int fd = -1;
    size_t sz = sizeof(fd);
    zmq_getsockopt(sock, ZMQ_FD, &fd, &sz);
    return fd;
}

static void epoll_modify(int epoll_fd, int fd, int op)
{
    struct epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd, op, fd, op == EPOLL_CTL_DEL ? nullptr : &ev);
}

static void close_peer(bacaro_t *self, PeerInfo &peer)
{
    zmq_disconnect(self->sub_sock, peer.pub_endpoint.c_str());
    if (peer.dealer_sock) {
        epoll_modify(self->epoll_fd, peer.dealer_fd, EPOLL_CTL_DEL);
        self->dealer_fd_to_filename.erase(peer.dealer_fd);
        zmq_close(peer.dealer_sock);
    }
}

int discovery_ensure_dealer(bacaro_t *self, const std::string &filename, PeerInfo &peer)
{
    if (peer.dealer_sock)
        return BACARO_OK;

    peer.dealer_sock = zmq_socket(self->zmq_ctx, ZMQ_DEALER);
    if (!peer.dealer_sock)
        return BACARO_EZMQ;

    if (zmq_connect(peer.dealer_sock, peer.rep_endpoint.c_str()) != 0) {
        zmq_close(peer.dealer_sock);
        peer.dealer_sock = nullptr;
        return BACARO_EZMQ;
    }

    peer.dealer_fd = get_zmq_fd(peer.dealer_sock);
    epoll_modify(self->epoll_fd, peer.dealer_fd, EPOLL_CTL_ADD);
    self->dealer_fd_to_filename[peer.dealer_fd] = filename;

    return BACARO_OK;
}

void discovery_epoll_add_zmq(bacaro_t *self, void *sock)
{
    epoll_modify(self->epoll_fd, get_zmq_fd(sock), EPOLL_CTL_ADD);
}

void discovery_epoll_remove_zmq(bacaro_t *self, void *sock)
{
    epoll_modify(self->epoll_fd, get_zmq_fd(sock), EPOLL_CTL_DEL);
}

static std::string ipc_endpoint(const bacaro_t *self, const std::string &filename)
{
    return "ipc://" + self->runtime_dir + "/" + filename;
}

void discovery_apply_subscriptions(bacaro_t *self)
{
    for (const auto &sub : self->subscriptions)
        zmq_setsockopt(self->sub_sock, ZMQ_SUBSCRIBE, sub.c_str(), sub.size());
}

int discovery_peer_connect(bacaro_t *self, const std::string &filename)
{
    if (self->peers.count(filename))
        return BACARO_OK;

    auto dot = filename.find('.');
    if (dot == std::string::npos)
        return BACARO_EINVAL;
    std::string peer_name = filename.substr(0, dot);

    // Derive the .rep filename from the .pub filename
    std::string rep_filename = filename.substr(0, filename.size() - 4) + ".rep";

    // ── Shared SUB socket: just connect to the new peer's PUB ────────────
    std::string pub_ep = ipc_endpoint(self, filename);
    if (zmq_connect(self->sub_sock, pub_ep.c_str()) != 0)
        return BACARO_EZMQ;

    std::string rep_ep = ipc_endpoint(self, rep_filename);
    self->peers[filename] = { nullptr, peer_name, pub_ep, rep_ep, -1 };

    // ── Lazy DEALER: only create if we have active subscriptions ─────────
    if (!self->subscriptions.empty()) {
        if (discovery_ensure_dealer(self, filename, self->peers[filename]) != BACARO_OK) {
            zmq_disconnect(self->sub_sock, pub_ep.c_str());
            self->peers.erase(filename);
            return BACARO_EZMQ;
        }

        for (const auto &prefix : self->subscriptions)
            snapshot_send_request(self->peers[filename].dealer_sock, prefix);
    }

    return BACARO_OK;
}

void discovery_peer_disconnect(bacaro_t *self, const std::string &filename)
{
    auto it = self->peers.find(filename);
    if (it == self->peers.end())
        return;

    close_peer(self, it->second);
    self->peers.erase(it);
    // Cache entries intentionally preserved
}

void discovery_process_inotify(bacaro_t *self)
{
    alignas(struct inotify_event) char buf[4096];

    while (true) {
        ssize_t len = read(self->inotify_fd, buf, sizeof(buf));
        if (len < 0)
            break;

        for (char *ptr = buf; ptr < buf + len; ) {
            auto *ev = reinterpret_cast<struct inotify_event *>(ptr);

            if (ev->len > 0) {
                std::string filename(ev->name);
                if (ends_with(filename, ".pub") && filename != self->own_pub) {
                    if (ev->mask & IN_CREATE)
                        discovery_peer_connect(self, filename);
                    else if (ev->mask & IN_DELETE)
                        discovery_peer_disconnect(self, filename);
                }
            }

            ptr += sizeof(struct inotify_event) + ev->len;
        }
    }
}

int discovery_init(bacaro_t *self)
{
    self->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (self->epoll_fd < 0)
        return BACARO_EZMQ;

    // Create shared SUB socket
    self->sub_sock = zmq_socket(self->zmq_ctx, ZMQ_SUB);
    if (!self->sub_sock)
        return BACARO_EZMQ;

    self->sub_fd = get_zmq_fd(self->sub_sock);
    epoll_modify(self->epoll_fd, self->sub_fd, EPOLL_CTL_ADD);

    self->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (self->inotify_fd < 0)
        return BACARO_EZMQ;

    self->inotify_wd = inotify_add_watch(
        self->inotify_fd, self->runtime_dir.c_str(), IN_CREATE | IN_DELETE);
    if (self->inotify_wd < 0)
        return BACARO_EZMQ;

    epoll_modify(self->epoll_fd, self->inotify_fd, EPOLL_CTL_ADD);

    // Register PUB and ROUTER in epoll; remember router's ZMQ_FD for dispatch
    discovery_epoll_add_zmq(self, self->pub_sock);
    discovery_epoll_add_zmq(self, self->router_sock);
    self->router_fd = get_zmq_fd(self->router_sock);

    // Scan for existing peers (inotify already watching — no race)
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(self->runtime_dir, ec)) {
        std::string filename = entry.path().filename().string();
        if (ends_with(filename, ".pub") && filename != self->own_pub)
            discovery_peer_connect(self, filename);
    }

    return BACARO_OK;
}

void discovery_cleanup(bacaro_t *self)
{
    for (auto &[filename, peer] : self->peers)
        close_peer(self, peer);
    self->peers.clear();

    if (self->sub_sock) {
        zmq_close(self->sub_sock);
        self->sub_sock = nullptr;
        self->sub_fd = -1;
    }

    if (self->inotify_fd >= 0) {
        close(self->inotify_fd);
        self->inotify_fd = -1;
        self->inotify_wd = -1;
    }

    if (self->epoll_fd >= 0) {
        close(self->epoll_fd);
        self->epoll_fd = -1;
    }
}
