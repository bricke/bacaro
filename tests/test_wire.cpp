#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "wire.h"
#include "bacaro.h"

TEST_CASE("wire_pack produces 4 frames")
{
    WireMessage msg;
    msg.topic      = "sensors.cpu.temperature";
    msg.publisher  = "powerd";
    msg.header     = { BACARO_WIRE_VERSION, BACARO_FLAG_NONE, 1, wire_now_us() };
    msg.payload    = { 0xca, 0x42, 0x28, 0x00, 0x00 }; // msgpack float32 42.0

    Frames frames = wire_pack(msg);

    REQUIRE(frames.size() == 4);

    // Frame 0: topic
    std::string topic(frames[0].begin(), frames[0].end());
    CHECK(topic == "sensors.cpu.temperature");

    // Frame 1: publisher
    std::string publisher(frames[1].begin(), frames[1].end());
    CHECK(publisher == "powerd");

    // Frame 2: header size
    CHECK(frames[2].size() == sizeof(WireHeader));

    // Frame 3: payload
    CHECK(frames[3] == msg.payload);
}

TEST_CASE("wire_unpack roundtrip")
{
    WireMessage original;
    original.topic     = "network.eth0.rx_bytes";
    original.publisher = "netd";
    original.header    = { BACARO_WIRE_VERSION, BACARO_FLAG_NONE, 42, 1234567890ULL };
    original.payload   = { 0xce, 0x00, 0x01, 0x86, 0xa0 }; // msgpack uint32 100000

    Frames frames = wire_pack(original);

    WireMessage decoded;
    int rc = wire_unpack(frames, decoded);

    REQUIRE(rc == BACARO_OK);
    CHECK(decoded.topic            == original.topic);
    CHECK(decoded.publisher        == original.publisher);
    CHECK(decoded.header.version   == original.header.version);
    CHECK(decoded.header.flags     == original.header.flags);
    CHECK(decoded.header.sequence  == original.header.sequence);
    CHECK(decoded.header.timestamp == original.header.timestamp);
    CHECK(decoded.payload          == original.payload);
}

TEST_CASE("wire_unpack rejects too few frames")
{
    Frames frames(3); // only 3 frames, need 4
    WireMessage out;
    CHECK(wire_unpack(frames, out) == BACARO_EINVAL);
}

TEST_CASE("wire_unpack rejects wrong header size")
{
    Frames frames(4);
    frames[0] = { 'a' };
    frames[1] = { 'b' };
    frames[2] = { 0x01, 0x02 }; // too short for WireHeader
    frames[3] = {};
    WireMessage out;
    CHECK(wire_unpack(frames, out) == BACARO_EINVAL);
}

TEST_CASE("wire_unpack rejects unknown version")
{
    WireMessage msg;
    msg.topic     = "foo.bar";
    msg.publisher = "test";
    msg.header    = { 0xFF, BACARO_FLAG_NONE, 0, 0 }; // bad version
    msg.payload   = {};

    Frames frames = wire_pack(msg);
    WireMessage out;
    CHECK(wire_unpack(frames, out) == BACARO_EINVAL);
}

TEST_CASE("wire_now_us returns a plausible timestamp")
{
    uint64_t t1 = wire_now_us();
    uint64_t t2 = wire_now_us();
    // 2020-01-01 in microseconds ~ 1577836800000000
    CHECK(t1 > 1577836800000000ULL);
    CHECK(t2 >= t1);
}
