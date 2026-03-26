#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Forward declaration to avoid circular include with cache.h
class Cache;

// Wire format version
static constexpr uint8_t BACARO_WIRE_VERSION = 2;

// Flags
static constexpr uint8_t BACARO_FLAG_NONE         = 0x00;
static constexpr uint8_t BACARO_FLAG_SNAPSHOT_REQ = 0x01;
static constexpr uint8_t BACARO_FLAG_SNAPSHOT_REP = 0x02;
static constexpr uint8_t BACARO_FLAG_SNAPSHOT_END = 0x04;

#pragma pack(push, 1)
struct WireHeader {
    uint8_t  version;    // wire format version
    uint8_t  flags;      // BACARO_FLAG_*
    uint64_t sequence;   // per-publisher monotonic counter
    uint64_t timestamp;  // microseconds since epoch
};
#pragma pack(pop)

static_assert(sizeof(WireHeader) == 18, "WireHeader size mismatch");

using Frame  = std::vector<uint8_t>;
using Frames = std::vector<Frame>;

struct WireMessage {
    std::string  topic;      // "domain.sub.property"
    WireHeader   header;
    Frame        payload;    // msgpack bytes
    std::string  publisher;  // carried on the wire since v2
};

// ── Publish encode/decode ─────────────────────────────────────────────────────

// Encode WireMessage into frames (no ZMQ dependency)
Frames wire_pack(const WireMessage &msg);

// Decode frames into WireMessage — returns BACARO_OK or BACARO_EINVAL
int wire_unpack(const Frames &frames, WireMessage &out);

// Send / receive frames over a ZMQ socket
int wire_send(void *sock, const Frames &frames);
int wire_recv(void *sock, Frames &out);

// ── Snapshot protocol ────────────────────────────────────────────────────────

// DEALER → ROUTER: send a snapshot request for `domain_prefix`
// (empty string = all domains)
int snapshot_send_request(void *dealer_sock, const std::string &domain_prefix);

// ROUTER side: receive one snapshot request, reply with all matching cache
// entries, then send an END marker.
// Expects the Cache to be passed in since wire.cpp has no bacaro_s dependency.
int snapshot_handle_request(void *router_sock, const Cache &cache);

// DEALER side: receive one snapshot reply frame into `out`.
// Sets `is_end = true` and returns BACARO_OK when the END marker arrives.
int snapshot_recv_one(void *dealer_sock, WireMessage &out, bool &is_end);

// ── Utilities ─────────────────────────────────────────────────────────────────

uint64_t wire_now_us();
