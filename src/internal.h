#pragma once

#include <zmq.h>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>

#include "bacaro.h"
#include "cache.h"

struct bacaro_proplist_s {
    CacheSnapshot entries; // vector of (path, CacheEntry)
};

struct PeerInfo {
    void        *sub_sock;    // ZMQ_SUB for live updates
    void        *dealer_sock; // ZMQ_DEALER for snapshot requests/replies
    std::string  name;        // peer process name
    int          sub_fd;      // ZMQ_FD of sub_sock (epoll)
    int          dealer_fd;   // ZMQ_FD of dealer_sock (epoll)
};

struct bacaro_s {
    std::string name;
    std::string runtime_dir;
    std::string uuid;
    std::string own_pub;  // "<name>.<uuid>.pub" — our own filename to ignore in discovery

    void *zmq_ctx     = nullptr;
    void *pub_sock    = nullptr;  // ZMQ_PUB
    void *router_sock = nullptr;  // ZMQ_ROUTER (snapshot server)
    int   router_fd   = -1;       // ZMQ_FD of router_sock (epoll)

    // Connected peers: keyed by peer filename (e.g. "powerd.<uuid>.pub")
    std::unordered_map<std::string, PeerInfo> peers;

    // Reverse maps for dispatch
    std::unordered_map<int, std::string> sub_fd_to_filename;    // sub   zmq_fd -> filename
    std::unordered_map<int, std::string> dealer_fd_to_filename; // dealer zmq_fd -> filename

    Cache    cache;
    uint64_t sequence = 0;  // per-process monotonic publish counter

    // Subscribed domain prefixes
    std::vector<std::string> subscriptions;

    // inotify + epoll
    int inotify_fd = -1;
    int inotify_wd = -1;
    int epoll_fd   = -1;

    // User callback
    bacaro_fn  on_update_cb   = nullptr;
    void     *on_update_data = nullptr;
};
