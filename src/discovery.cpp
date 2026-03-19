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

void discovery_apply_subscriptions(bacaro_t *self, void *sub_sock)
{
    for (const auto &sub : self->subscriptions)
        zmq_setsockopt(sub_sock, ZMQ_SUBSCRIBE, sub.c_str(), sub.size());
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

    // ── SUB socket ───────────────────────────────────────────────────────────
    void *sub_sock = zmq_socket(self->zmq_ctx, ZMQ_SUB);
    if (!sub_sock)
        return BACARO_EZMQ;

    if (zmq_connect(sub_sock, ipc_endpoint(self, filename).c_str()) != 0) {
        zmq_close(sub_sock);
        return BACARO_EZMQ;
    }
    discovery_apply_subscriptions(self, sub_sock);

    int sub_fd = get_zmq_fd(sub_sock);
    epoll_modify(self->epoll_fd, sub_fd, EPOLL_CTL_ADD);

    // ── DEALER socket ────────────────────────────────────────────────────────
    void *dealer_sock = zmq_socket(self->zmq_ctx, ZMQ_DEALER);
    if (!dealer_sock) {
        zmq_close(sub_sock);
        return BACARO_EZMQ;
    }

    if (zmq_connect(dealer_sock, ipc_endpoint(self, rep_filename).c_str()) != 0) {
        zmq_close(sub_sock);
        zmq_close(dealer_sock);
        return BACARO_EZMQ;
    }

    int dealer_fd = get_zmq_fd(dealer_sock);
    epoll_modify(self->epoll_fd, dealer_fd, EPOLL_CTL_ADD);

    // Register in maps
    self->peers[filename]              = { sub_sock, dealer_sock, peer_name, sub_fd, dealer_fd };
    self->sub_fd_to_filename[sub_fd]   = filename;
    self->dealer_fd_to_filename[dealer_fd] = filename;

    // Send snapshot request for each subscribed domain
    for (const auto &prefix : self->subscriptions)
        snapshot_send_request(dealer_sock, prefix);

    return BACARO_OK;
}

void discovery_peer_disconnect(bacaro_t *self, const std::string &filename)
{
    auto it = self->peers.find(filename);
    if (it == self->peers.end())
        return;

    auto &peer = it->second;

    epoll_modify(self->epoll_fd, peer.sub_fd, EPOLL_CTL_DEL);
    epoll_modify(self->epoll_fd, peer.dealer_fd, EPOLL_CTL_DEL);

    self->sub_fd_to_filename.erase(peer.sub_fd);
    self->dealer_fd_to_filename.erase(peer.dealer_fd);

    zmq_close(peer.sub_sock);
    zmq_close(peer.dealer_sock);

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

    self->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (self->inotify_fd < 0)
        return BACARO_EZMQ;

    self->inotify_wd = inotify_add_watch(
        self->inotify_fd, self->runtime_dir.c_str(), IN_CREATE | IN_DELETE);
    if (self->inotify_wd < 0)
        return BACARO_EZMQ;

    struct epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = self->inotify_fd;
    epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, self->inotify_fd, &ev);

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
    for (auto &[filename, peer] : self->peers) {
        epoll_modify(self->epoll_fd, peer.sub_fd, EPOLL_CTL_DEL);
        epoll_modify(self->epoll_fd, peer.dealer_fd, EPOLL_CTL_DEL);
        zmq_close(peer.sub_sock);
        zmq_close(peer.dealer_sock);
    }
    self->peers.clear();
    self->sub_fd_to_filename.clear();
    self->dealer_fd_to_filename.clear();

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
