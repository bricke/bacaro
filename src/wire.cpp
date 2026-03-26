#include "wire.h"
#include "cache.h"
#include "bacaro.h"

#include <zmq.h>
#include <chrono>
#include <cstring>

uint64_t wire_now_us()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(system_clock::now().time_since_epoch()).count()
    );
}

// ── Publish encode/decode ────────────────────────────────────────────────────

Frames wire_pack(const WireMessage &msg)
{
    Frames frames(4);

    frames[0].assign(msg.topic.begin(), msg.topic.end());
    frames[1].assign(msg.publisher.begin(), msg.publisher.end());

    frames[2].resize(sizeof(WireHeader));
    std::memcpy(frames[2].data(), &msg.header, sizeof(WireHeader));

    frames[3] = msg.payload;

    return frames;
}

int wire_unpack(const Frames &frames, WireMessage &out)
{
    if (frames.size() < 4)
        return BACARO_EINVAL;

    if (frames[2].size() != sizeof(WireHeader))
        return BACARO_EINVAL;

    out.topic.assign(frames[0].begin(), frames[0].end());
    out.publisher.assign(frames[1].begin(), frames[1].end());

    std::memcpy(&out.header, frames[2].data(), sizeof(WireHeader));

    if (out.header.version != BACARO_WIRE_VERSION)
        return BACARO_EINVAL;

    out.payload = frames[3];

    return BACARO_OK;
}

int wire_send(void *sock, const Frames &frames)
{
    for (size_t i = 0; i < frames.size(); ++i) {
        int flags = (i < frames.size() - 1) ? ZMQ_SNDMORE : 0;
        if (zmq_send(sock, frames[i].data(), frames[i].size(), flags) < 0)
            return BACARO_EZMQ;
    }
    return BACARO_OK;
}

int wire_recv(void *sock, Frames &out)
{
    out.clear();
    int more;
    do {
        zmq_msg_t msg;
        zmq_msg_init(&msg);

        if (zmq_msg_recv(&msg, sock, 0) < 0) {
            zmq_msg_close(&msg);
            return BACARO_EZMQ;
        }

        const uint8_t *data = static_cast<const uint8_t *>(zmq_msg_data(&msg));
        out.emplace_back(data, data + zmq_msg_size(&msg));

        size_t sz = sizeof(more);
        zmq_getsockopt(sock, ZMQ_RCVMORE, &more, &sz);
        zmq_msg_close(&msg);
    } while (more);

    return BACARO_OK;
}

// ── Snapshot protocol ────────────────────────────────────────────────────────

int snapshot_send_request(void *dealer_sock, const std::string &domain_prefix)
{
    // Frame 0: flag byte
    uint8_t flag = BACARO_FLAG_SNAPSHOT_REQ;
    if (zmq_send(dealer_sock, &flag, 1, ZMQ_SNDMORE) < 0)
        return BACARO_EZMQ;

    // Frame 1: domain prefix
    if (zmq_send(dealer_sock, domain_prefix.data(), domain_prefix.size(), 0) < 0)
        return BACARO_EZMQ;

    return BACARO_OK;
}

int snapshot_handle_request(void *router_sock, const Cache &cache)
{
    // ROUTER receives: [identity][flag][domain_prefix]
    Frames frames;
    if (wire_recv(router_sock, frames) != BACARO_OK)
        return BACARO_EZMQ;

    // Need at least: identity + flag + prefix
    if (frames.size() < 3)
        return BACARO_EINVAL;

    const Frame &identity = frames[0];
    uint8_t      flag     = frames[1].empty() ? 0 : frames[1][0];

    if (flag != BACARO_FLAG_SNAPSHOT_REQ)
        return BACARO_EINVAL;

    std::string prefix(frames[2].begin(), frames[2].end());

    auto send_frame = [&](const void *data, size_t len, int flags) -> int {
        return zmq_send(router_sock, data, len, flags) < 0 ? BACARO_EZMQ : BACARO_OK;
    };

    // Send one reply per matching cache entry; break on any send failure so we
    // don't corrupt a half-written multipart message on the wire.
    auto entries = cache.get_prefix(prefix);
    for (const auto &[path, entry] : entries) {
        // [identity][REP_flag][topic][publisher][header][payload]
        WireHeader hdr = {
            BACARO_WIRE_VERSION,
            BACARO_FLAG_SNAPSHOT_REP,
            entry.sequence,
            entry.timestamp
        };

        uint8_t rep_flag = BACARO_FLAG_SNAPSHOT_REP;
        if (send_frame(identity.data(), identity.size(), ZMQ_SNDMORE) != BACARO_OK
         || send_frame(&rep_flag, 1, ZMQ_SNDMORE) != BACARO_OK
         || send_frame(path.data(), path.size(), ZMQ_SNDMORE) != BACARO_OK
         || send_frame(entry.publisher.data(), entry.publisher.size(), ZMQ_SNDMORE) != BACARO_OK
         || send_frame(&hdr, sizeof(hdr), ZMQ_SNDMORE) != BACARO_OK
         || send_frame(entry.payload.data(), entry.payload.size(), 0) != BACARO_OK)
            break;
    }

    // Always send END marker — even after a send failure — so the peer is
    // never left blocking indefinitely waiting for a reply that will not come.
    send_frame(identity.data(), identity.size(), ZMQ_SNDMORE);
    uint8_t end_flag = BACARO_FLAG_SNAPSHOT_END;
    send_frame(&end_flag, 1, 0);

    return BACARO_OK;
}

int snapshot_recv_one(void *dealer_sock, WireMessage &out, bool &is_end)
{
    is_end = false;

    // DEALER receives: [flag][...rest]
    Frames frames;
    if (wire_recv(dealer_sock, frames) != BACARO_OK)
        return BACARO_EZMQ;

    if (frames.empty())
        return BACARO_EINVAL;

    uint8_t flag = frames[0].empty() ? 0 : frames[0][0];

    if (flag == BACARO_FLAG_SNAPSHOT_END) {
        is_end = true;
        return BACARO_OK;
    }

    if (flag != BACARO_FLAG_SNAPSHOT_REP || frames.size() < 5)
        return BACARO_EINVAL;

    if (frames[3].size() != sizeof(WireHeader))
        return BACARO_EINVAL;

    out.topic.assign(frames[1].begin(), frames[1].end());
    out.publisher.assign(frames[2].begin(), frames[2].end());
    std::memcpy(&out.header, frames[3].data(), sizeof(WireHeader));
    out.payload = frames[4];

    return BACARO_OK;
}
