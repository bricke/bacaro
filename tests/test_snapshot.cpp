#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <unistd.h>
#include <msgpack.hpp>

#include "bacaro.h"
#include "internal.h"
#include "wire.h"

static const char *TEST_DIR = "/tmp/bacaro_test_snapshot";

struct Fixture {
    Fixture()  { setenv("BACARO_RUNTIME_DIR", TEST_DIR, 1);
                 std::filesystem::create_directories(TEST_DIR); }
    ~Fixture() { unsetenv("BACARO_RUNTIME_DIR");
                 std::filesystem::remove_all(TEST_DIR); }
};

// Helper: pack a float value as msgpack
static Frame pack_float(float v)
{
    msgpack::sbuffer buf;
    msgpack::pack(buf, v);
    return Frame(buf.data(), buf.data() + buf.size());
}

// Helper: pump dispatch until condition is met or timeout
static bool wait_for(bacaro_t *n, std::function<bool()> cond, int tries = 50)
{
    for (int i = 0; i < tries; ++i) {
        if (cond()) return true;
        usleep(5000);
        bacaro_dispatch(n);
    }
    return cond();
}

TEST_CASE("late joiner receives snapshot of existing properties")
{
    Fixture f;

    // Alpha starts, seeds its own cache and subscribes
    bacaro_t *alpha = bacaro_new("alpha");
    REQUIRE(alpha != nullptr);

    bacaro_subscribe(alpha, "sensors");

    // Manually seed alpha's cache (simulating that alpha published these)
    alpha->cache.set("sensors.cpu.temp",  pack_float(72.5f), "alpha", 1, 1000);
    alpha->cache.set("sensors.gpu.temp",  pack_float(65.0f), "alpha", 2, 2000);
    alpha->cache.set("network.eth0.rx",   pack_float(1024.f), "alpha", 3, 3000);

    // Beta joins late and subscribes to "sensors"
    bacaro_t *beta = bacaro_new("beta");
    REQUIRE(beta != nullptr);

    bacaro_subscribe(beta, "sensors");

    // Alpha discovers beta via inotify, beta already found alpha at scan time
    // Pump both until beta's cache is populated
    bool seeded = wait_for(beta, [&]() {
        return beta->cache.get("sensors.cpu.temp") != nullptr
            && beta->cache.get("sensors.gpu.temp") != nullptr;
    });

    // Also pump alpha so it handles beta's snapshot request
    for (int i = 0; i < 50; ++i) {
        bacaro_dispatch(alpha);
        bacaro_dispatch(beta);
        usleep(2000);
    }

    REQUIRE(beta->cache.get("sensors.cpu.temp") != nullptr);
    REQUIRE(beta->cache.get("sensors.gpu.temp") != nullptr);

    // "network.eth0.rx" should NOT be in beta's cache (only subscribed to "sensors")
    CHECK(beta->cache.get("network.eth0.rx") == nullptr);

    // Publisher should be inferred as "alpha"
    CHECK(beta->cache.get("sensors.cpu.temp")->publisher == "alpha");

    bacaro_destroy(&alpha);
    bacaro_destroy(&beta);
}

TEST_CASE("mid-life subscribe triggers snapshot for new domain")
{
    Fixture f;

    bacaro_t *alpha = bacaro_new("alpha");
    bacaro_t *beta  = bacaro_new("beta");
    REQUIRE(alpha != nullptr);
    REQUIRE(beta  != nullptr);

    // Seed alpha's cache
    alpha->cache.set("network.eth0.rx", pack_float(512.f), "alpha", 1, 1000);
    alpha->cache.set("sensors.temp",    pack_float(40.f),  "alpha", 2, 2000);

    // Beta subscribes to sensors first
    bacaro_subscribe(beta, "sensors");

    // Let them discover each other
    usleep(20000);
    bacaro_dispatch(alpha);
    bacaro_dispatch(beta);

    // Beta now subscribes to network mid-life
    bacaro_subscribe(beta, "network");

    // Pump until cache is populated
    for (int i = 0; i < 100; ++i) {
        bacaro_dispatch(alpha);
        bacaro_dispatch(beta);
        usleep(2000);
    }

    CHECK(beta->cache.get("network.eth0.rx") != nullptr);
    CHECK(beta->cache.get("sensors.temp")    != nullptr);

    bacaro_destroy(&alpha);
    bacaro_destroy(&beta);
}

TEST_CASE("subscribe_all receives full snapshot")
{
    Fixture f;

    bacaro_t *alpha = bacaro_new("alpha");
    bacaro_t *beta  = bacaro_new("beta");
    REQUIRE(alpha != nullptr);
    REQUIRE(beta  != nullptr);

    alpha->cache.set("sensors.temp",  pack_float(55.f), "alpha", 1, 1000);
    alpha->cache.set("network.rx",    pack_float(10.f), "alpha", 2, 2000);
    alpha->cache.set("system.uptime", pack_float(99.f), "alpha", 3, 3000);

    bacaro_subscribe_all(beta);

    for (int i = 0; i < 100; ++i) {
        bacaro_dispatch(alpha);
        bacaro_dispatch(beta);
        usleep(2000);
    }

    CHECK(beta->cache.get("sensors.temp")  != nullptr);
    CHECK(beta->cache.get("network.rx")    != nullptr);
    CHECK(beta->cache.get("system.uptime") != nullptr);

    bacaro_destroy(&alpha);
    bacaro_destroy(&beta);
}
