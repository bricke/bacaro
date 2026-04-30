#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <unistd.h>
#include <msgpack.hpp>

#include "bacaro.h"
#include "internal.h"
#include "wire.h"
#include "test_helpers.h"

static const char *TEST_DIR = "/tmp/bacaro_test_pubsub";

// ── bacaro_set ────────────────────────────────────────────────────────────────

TEST_CASE("bacaro_set returns BACARO_OK")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    Frame v = pack_float(42.0);
    CHECK(bacaro_set(a, "sensors.temp", v.data(), v.size()) == BACARO_OK);

    bacaro_destroy(&a);
}

TEST_CASE("bacaro_set rejects null arguments")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    Frame v = pack_float(1.0);
    CHECK(bacaro_set(nullptr, "sensors.temp", v.data(), v.size()) == BACARO_EINVAL);
    CHECK(bacaro_set(a, nullptr, v.data(), v.size())              == BACARO_EINVAL);
    CHECK(bacaro_set(a, "sensors.temp", nullptr, v.size())        == BACARO_EINVAL);

    bacaro_destroy(&a);
}

TEST_CASE("bacaro_set immediately updates local cache")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    Frame v = pack_float(55.5);
    bacaro_set(a, "sensors.cpu.temp", v.data(), v.size());

    const uint8_t *out; size_t out_len;
    REQUIRE(bacaro_get(a, "sensors.cpu.temp", &out, &out_len) == BACARO_OK);
    CHECK(unpack_float(out, out_len) == doctest::Approx(55.5));

    bacaro_destroy(&a);
}

TEST_CASE("bacaro_set stores self as publisher")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("powerd", nullptr);
    REQUIRE(a != nullptr);

    Frame v = pack_int(100);
    bacaro_set(a, "system.battery.level", v.data(), v.size());

    const char *pub = bacaro_get_publisher(a, "system.battery.level");
    REQUIRE(pub != nullptr);
    CHECK(std::string(pub) == "powerd");

    bacaro_destroy(&a);
}

TEST_CASE("bacaro_set increments sequence counter")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    Frame v = pack_int(1);
    bacaro_set(a, "a.b", v.data(), v.size());
    bacaro_set(a, "a.c", v.data(), v.size());
    bacaro_set(a, "a.d", v.data(), v.size());

    CHECK(a->cache.get("a.b")->sequence == 1);
    CHECK(a->cache.get("a.c")->sequence == 2);
    CHECK(a->cache.get("a.d")->sequence == 3);

    bacaro_destroy(&a);
}

TEST_CASE("bacaro_set last-write-wins on same path")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    Frame v1 = pack_float(10.0);
    Frame v2 = pack_float(20.0);
    bacaro_set(a, "sensors.temp", v1.data(), v1.size());
    bacaro_set(a, "sensors.temp", v2.data(), v2.size());

    const uint8_t *out; size_t out_len;
    REQUIRE(bacaro_get(a, "sensors.temp", &out, &out_len) == BACARO_OK);
    CHECK(unpack_float(out, out_len) == doctest::Approx(20.0));

    bacaro_destroy(&a);
}

TEST_CASE("bacaro_set supports various msgpack types")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    // integer
    Frame vi = pack_int(42);
    bacaro_set(a, "data.int", vi.data(), vi.size());

    // float
    Frame vf = pack_float(3.14);
    bacaro_set(a, "data.float", vf.data(), vf.size());

    // string
    Frame vs = pack_str("hello bacaro");
    bacaro_set(a, "data.str", vs.data(), vs.size());

    // array
    msgpack::sbuffer buf;
    msgpack::pack(buf, std::vector<int>{1, 2, 3});
    Frame va(buf.data(), buf.data() + buf.size());
    bacaro_set(a, "data.array", va.data(), va.size());

    const uint8_t *out; size_t len;
    REQUIRE(bacaro_get(a, "data.int",   &out, &len) == BACARO_OK);
    CHECK(unpack_int(out, len) == 42);

    REQUIRE(bacaro_get(a, "data.float", &out, &len) == BACARO_OK);
    CHECK(unpack_float(out, len) == doctest::Approx(3.14));

    REQUIRE(bacaro_get(a, "data.str",   &out, &len) == BACARO_OK);
    auto oh = msgpack::unpack(reinterpret_cast<const char *>(out), len);
    CHECK(oh.get().as<std::string>() == "hello bacaro");

    REQUIRE(bacaro_get(a, "data.array", &out, &len) == BACARO_OK);

    bacaro_destroy(&a);
}

// ── bacaro_get ────────────────────────────────────────────────────────────────

TEST_CASE("bacaro_get returns BACARO_ENOTFOUND for unknown path")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    const uint8_t *out; size_t len;
    CHECK(bacaro_get(a, "does.not.exist", &out, &len) == BACARO_ENOTFOUND);

    bacaro_destroy(&a);
}

TEST_CASE("bacaro_get rejects null arguments")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    const uint8_t *out; size_t len;
    CHECK(bacaro_get(nullptr, "sensors.temp", &out, &len) == BACARO_EINVAL);
    CHECK(bacaro_get(a, nullptr, &out, &len)              == BACARO_EINVAL);
    CHECK(bacaro_get(a, "sensors.temp", nullptr, &len)    == BACARO_EINVAL);
    CHECK(bacaro_get(a, "sensors.temp", &out, nullptr)    == BACARO_EINVAL);

    bacaro_destroy(&a);
}

// ── bacaro_get_publisher ──────────────────────────────────────────────────────

TEST_CASE("bacaro_get_publisher returns null for unknown path")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    CHECK(bacaro_get_publisher(a, "unknown.path") == nullptr);
    CHECK(bacaro_get_publisher(nullptr, "sensors.temp") == nullptr);
    CHECK(bacaro_get_publisher(a, nullptr) == nullptr);

    bacaro_destroy(&a);
}

// ── Pub/Sub integration ──────────────────────────────────────────────────────

TEST_CASE("subscriber receives published message via dispatch")
{
    Fixture f(TEST_DIR);

    bacaro_t *pub = bacaro_new("publisher", nullptr);
    bacaro_t *sub = bacaro_new("subscriber", nullptr);
    REQUIRE(pub != nullptr);
    REQUIRE(sub != nullptr);

    bacaro_subscribe(sub, "sensors");

    // Let them discover each other
    usleep(20000);
    bacaro_dispatch(pub);
    bacaro_dispatch(sub);

    Frame v = pack_float(98.6);
    bacaro_set(pub, "sensors.body.temp", v.data(), v.size());

    bool received = pump(pub, sub, [&]() {
        return sub->cache.get("sensors.body.temp") != nullptr;
    });

    REQUIRE(received);

    const uint8_t *out; size_t len;
    REQUIRE(bacaro_get(sub, "sensors.body.temp", &out, &len) == BACARO_OK);
    CHECK(unpack_float(out, len) == doctest::Approx(98.6));

    // Publisher name correctly inferred from socket
    const char *publisher = bacaro_get_publisher(sub, "sensors.body.temp");
    REQUIRE(publisher != nullptr);
    CHECK(std::string(publisher) == "publisher");

    bacaro_destroy(&pub);
    bacaro_destroy(&sub);
}

TEST_CASE("subscriber does not receive messages outside subscribed domain")
{
    Fixture f(TEST_DIR);

    bacaro_t *pub = bacaro_new("publisher", nullptr);
    bacaro_t *sub = bacaro_new("subscriber", nullptr);
    REQUIRE(pub != nullptr);
    REQUIRE(sub != nullptr);

    bacaro_subscribe(sub, "sensors");

    usleep(20000);
    bacaro_dispatch(pub);
    bacaro_dispatch(sub);

    Frame v = pack_float(1.0);
    bacaro_set(pub, "network.eth0.rx",    v.data(), v.size()); // not subscribed
    bacaro_set(pub, "sensors.cpu.temp",   v.data(), v.size()); // subscribed

    // Pump until we see the subscribed one
    pump(pub, sub, [&]() {
        return sub->cache.get("sensors.cpu.temp") != nullptr;
    });

    CHECK(sub->cache.get("sensors.cpu.temp") != nullptr);
    CHECK(sub->cache.get("network.eth0.rx")  == nullptr);

    bacaro_destroy(&pub);
    bacaro_destroy(&sub);
}

TEST_CASE("subscribe_all receives all published messages")
{
    Fixture f(TEST_DIR);

    bacaro_t *pub = bacaro_new("publisher", nullptr);
    bacaro_t *sub = bacaro_new("subscriber", nullptr);
    REQUIRE(pub != nullptr);
    REQUIRE(sub != nullptr);

    bacaro_subscribe_all(sub);

    usleep(20000);
    bacaro_dispatch(pub);
    bacaro_dispatch(sub);

    Frame v = pack_float(1.0);
    bacaro_set(pub, "sensors.temp",  v.data(), v.size());
    bacaro_set(pub, "network.rx",    v.data(), v.size());
    bacaro_set(pub, "system.uptime", v.data(), v.size());

    bool received = pump(pub, sub, [&]() {
        return sub->cache.get("sensors.temp")  != nullptr
            && sub->cache.get("network.rx")    != nullptr
            && sub->cache.get("system.uptime") != nullptr;
    });

    CHECK(received);

    bacaro_destroy(&pub);
    bacaro_destroy(&sub);
}

TEST_CASE("on_update callback fires on received message")
{
    Fixture f(TEST_DIR);

    bacaro_t *pub = bacaro_new("publisher", nullptr);
    bacaro_t *sub = bacaro_new("subscriber", nullptr);
    REQUIRE(pub != nullptr);
    REQUIRE(sub != nullptr);

    bacaro_subscribe_all(sub);

    int    callback_count = 0;
    std::string last_path;
    bacaro_on_update(sub, [](bacaro_t *, const char *path, const uint8_t *, size_t, void *ud) {
        auto *ctx = reinterpret_cast<std::pair<int *, std::string *> *>(ud);
        ++(*ctx->first);
        *ctx->second = path;
    }, new std::pair<int *, std::string *>(&callback_count, &last_path));

    usleep(20000);
    bacaro_dispatch(pub);
    bacaro_dispatch(sub);

    Frame v = pack_float(1.0);
    bacaro_set(pub, "sensors.humidity", v.data(), v.size());

    pump(pub, sub, [&]() { return callback_count > 0; });

    CHECK(callback_count > 0);
    CHECK(last_path == "sensors.humidity");

    bacaro_destroy(&pub);
    bacaro_destroy(&sub);
}

TEST_CASE("unsubscribe stops receiving messages for that domain")
{
    Fixture f(TEST_DIR);

    bacaro_t *pub = bacaro_new("publisher", nullptr);
    bacaro_t *sub = bacaro_new("subscriber", nullptr);
    REQUIRE(pub != nullptr);
    REQUIRE(sub != nullptr);

    bacaro_subscribe(sub, "sensors");
    bacaro_subscribe(sub, "network");

    usleep(20000);
    bacaro_dispatch(pub);
    bacaro_dispatch(sub);

    // Confirm sensors works
    Frame v = pack_float(1.0);
    bacaro_set(pub, "sensors.temp", v.data(), v.size());
    pump(pub, sub, [&]() { return sub->cache.get("sensors.temp") != nullptr; });
    REQUIRE(sub->cache.get("sensors.temp") != nullptr);

    // Unsubscribe from sensors, then publish a new sensors property
    bacaro_unsubscribe(sub, "sensors");
    bacaro_set(pub, "sensors.pressure", v.data(), v.size());

    // Pump; sensors.pressure should not arrive
    for (int i = 0; i < 50; ++i) {
        bacaro_dispatch(pub);
        bacaro_dispatch(sub);
        usleep(3000);
    }
    CHECK(sub->cache.get("sensors.pressure") == nullptr);

    // Network still works
    bacaro_set(pub, "network.eth0.rx", v.data(), v.size());
    pump(pub, sub, [&]() { return sub->cache.get("network.eth0.rx") != nullptr; });
    CHECK(sub->cache.get("network.eth0.rx") != nullptr);

    bacaro_destroy(&pub);
    bacaro_destroy(&sub);
}

TEST_CASE("multiple publishers, one subscriber")
{
    Fixture f(TEST_DIR);

    bacaro_t *p1  = bacaro_new("proc1", nullptr);
    bacaro_t *p2  = bacaro_new("proc2", nullptr);
    bacaro_t *sub = bacaro_new("monitor", nullptr);
    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    REQUIRE(sub != nullptr);

    bacaro_subscribe_all(sub);

    usleep(30000);
    bacaro_dispatch(p1); bacaro_dispatch(p2); bacaro_dispatch(sub);

    Frame v = pack_float(1.0);
    bacaro_set(p1, "sensors.cpu.temp",   v.data(), v.size());
    bacaro_set(p2, "sensors.gpu.temp",   v.data(), v.size());

    bool received = pump(p1, sub, [&]() {
        bacaro_dispatch(p2);
        return sub->cache.get("sensors.cpu.temp") != nullptr
            && sub->cache.get("sensors.gpu.temp") != nullptr;
    });

    CHECK(received);
    CHECK(std::string(bacaro_get_publisher(sub, "sensors.cpu.temp")) == "proc1");
    CHECK(std::string(bacaro_get_publisher(sub, "sensors.gpu.temp")) == "proc2");

    bacaro_destroy(&p1);
    bacaro_destroy(&p2);
    bacaro_destroy(&sub);
}

TEST_CASE("property published by dead process stays in cache")
{
    Fixture f(TEST_DIR);

    bacaro_t *pub = bacaro_new("publisher", nullptr);
    bacaro_t *sub = bacaro_new("subscriber", nullptr);
    REQUIRE(pub != nullptr);
    REQUIRE(sub != nullptr);

    bacaro_subscribe_all(sub);
    usleep(20000);
    bacaro_dispatch(pub); bacaro_dispatch(sub);

    Frame v = pack_float(42.0);
    bacaro_set(pub, "sensors.temp", v.data(), v.size());

    pump(pub, sub, [&]() { return sub->cache.get("sensors.temp") != nullptr; });
    REQUIRE(sub->cache.get("sensors.temp") != nullptr);

    // Publisher dies
    bacaro_destroy(&pub);
    usleep(20000);
    bacaro_dispatch(sub); // process IN_DELETE inotify event

    // Peer gone but cache survives
    CHECK(sub->peers.empty());
    CHECK(sub->cache.get("sensors.temp") != nullptr);

    const uint8_t *out; size_t len;
    REQUIRE(bacaro_get(sub, "sensors.temp", &out, &len) == BACARO_OK);
    CHECK(unpack_float(out, len) == doctest::Approx(42.0));

    bacaro_destroy(&sub);
}
