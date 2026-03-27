#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <unistd.h>

#include "bacaro.h"
#include "internal.h"
#include "wire.h"
#include "test_helpers.h"

static const char *TEST_DIR = "/tmp/bacaro_test_snapshot";

TEST_CASE("late joiner receives snapshot of existing properties")
{
    Fixture f(TEST_DIR);

    // Alpha starts, seeds its own cache and subscribes
    bacaro_t *alpha = bacaro_new("alpha");
    REQUIRE(alpha != nullptr);

    bacaro_subscribe(alpha, "sensors");

    // Manually seed alpha's cache (simulating that alpha published these)
    alpha->cache.set("sensors.cpu.temp",  pack_float(72.5), "alpha", 1, 1000);
    alpha->cache.set("sensors.gpu.temp",  pack_float(65.0), "alpha", 2, 2000);
    alpha->cache.set("network.eth0.rx",   pack_float(1024.), "alpha", 3, 3000);

    // Beta joins late and subscribes to "sensors"
    bacaro_t *beta = bacaro_new("beta");
    REQUIRE(beta != nullptr);

    bacaro_subscribe(beta, "sensors");

    // Pump both until beta's cache is populated
    bool seeded = pump(alpha, beta, [&]() {
        return beta->cache.get("sensors.cpu.temp") != nullptr
            && beta->cache.get("sensors.gpu.temp") != nullptr;
    });

    REQUIRE(seeded);
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
    Fixture f(TEST_DIR);

    bacaro_t *alpha = bacaro_new("alpha");
    bacaro_t *beta  = bacaro_new("beta");
    REQUIRE(alpha != nullptr);
    REQUIRE(beta  != nullptr);

    // Seed alpha's cache
    alpha->cache.set("network.eth0.rx", pack_float(512.), "alpha", 1, 1000);
    alpha->cache.set("sensors.temp",    pack_float(40.),  "alpha", 2, 2000);

    // Beta subscribes to sensors first
    bacaro_subscribe(beta, "sensors");

    // Wait for discovery + initial snapshot to complete
    bool sensors_ok = pump(alpha, beta, [&]() {
        return beta->cache.get("sensors.temp") != nullptr;
    });
    REQUIRE(sensors_ok);

    // Beta now subscribes to network mid-life
    bacaro_subscribe(beta, "network");

    bool ok = pump(alpha, beta, [&]() {
        return beta->cache.get("network.eth0.rx") != nullptr;
    });

    CHECK(ok);
    CHECK(beta->cache.get("network.eth0.rx") != nullptr);
    CHECK(beta->cache.get("sensors.temp")    != nullptr);

    bacaro_destroy(&alpha);
    bacaro_destroy(&beta);
}

TEST_CASE("subscribe_all receives full snapshot")
{
    Fixture f(TEST_DIR);

    bacaro_t *alpha = bacaro_new("alpha");
    bacaro_t *beta  = bacaro_new("beta");
    REQUIRE(alpha != nullptr);
    REQUIRE(beta  != nullptr);

    alpha->cache.set("sensors.temp",  pack_float(55.), "alpha", 1, 1000);
    alpha->cache.set("network.rx",    pack_float(10.), "alpha", 2, 2000);
    alpha->cache.set("system.uptime", pack_float(99.), "alpha", 3, 3000);

    bacaro_subscribe_all(beta);

    bool ok = pump(alpha, beta, [&]() {
        return beta->cache.get("sensors.temp")  != nullptr
            && beta->cache.get("network.rx")    != nullptr
            && beta->cache.get("system.uptime") != nullptr;
    });

    CHECK(ok);
    CHECK(beta->cache.get("sensors.temp")  != nullptr);
    CHECK(beta->cache.get("network.rx")    != nullptr);
    CHECK(beta->cache.get("system.uptime") != nullptr);

    bacaro_destroy(&alpha);
    bacaro_destroy(&beta);
}
