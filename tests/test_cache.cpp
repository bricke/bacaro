#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "cache.h"

static Frame make_payload(uint8_t byte) { return { byte }; }

TEST_CASE("set and get roundtrip")
{
    Cache c;
    c.set("sensors.cpu.temperature", make_payload(0x01), "powerd", 1, 1000);

    const CacheEntry *e = c.get("sensors.cpu.temperature");
    REQUIRE(e != nullptr);
    CHECK(e->payload    == make_payload(0x01));
    CHECK(e->publisher  == "powerd");
    CHECK(e->sequence   == 1);
    CHECK(e->timestamp  == 1000);
}

TEST_CASE("get returns nullptr for unknown path")
{
    Cache c;
    CHECK(c.get("does.not.exist") == nullptr);
}

TEST_CASE("last-write-wins on overwrite")
{
    Cache c;
    c.set("sensors.cpu.temperature", make_payload(0x01), "powerd",  1, 1000);
    c.set("sensors.cpu.temperature", make_payload(0x02), "monitord", 2, 2000);

    const CacheEntry *e = c.get("sensors.cpu.temperature");
    REQUIRE(e != nullptr);
    CHECK(e->payload   == make_payload(0x02));
    CHECK(e->publisher == "monitord");
    CHECK(e->sequence  == 2);
}

TEST_CASE("get_prefix returns matching entries")
{
    Cache c;
    c.set("sensors.cpu.temperature", make_payload(0x01), "p", 1, 0);
    c.set("sensors.cpu.load",        make_payload(0x02), "p", 2, 0);
    c.set("sensors.gpu.temperature", make_payload(0x03), "p", 3, 0);
    c.set("network.eth0.rx",         make_payload(0x04), "p", 4, 0);

    SUBCASE("prefix matches subtree")
    {
        auto result = c.get_prefix("sensors.cpu");
        CHECK(result.size() == 2);
    }

    SUBCASE("prefix matches top-level domain")
    {
        auto result = c.get_prefix("sensors");
        CHECK(result.size() == 3);
    }

    SUBCASE("empty prefix matches everything")
    {
        auto result = c.get_prefix("");
        CHECK(result.size() == 4);
    }

    SUBCASE("unknown prefix returns empty")
    {
        auto result = c.get_prefix("bluetooth");
        CHECK(result.size() == 0);
    }
}

TEST_CASE("get_prefix does not match partial segment names")
{
    Cache c;
    c.set("sensor.value",  make_payload(0x01), "p", 1, 0);
    c.set("sensors.value", make_payload(0x02), "p", 2, 0);

    // "sensor" must not match "sensors.value"
    auto result = c.get_prefix("sensor");
    REQUIRE(result.size() == 1);
    CHECK(result[0].first == "sensor.value");
}

TEST_CASE("get_prefix exact path match")
{
    Cache c;
    c.set("a.b", make_payload(0x01), "p", 1, 0);

    auto result = c.get_prefix("a.b");
    REQUIRE(result.size() == 1);
    CHECK(result[0].first == "a.b");
}

TEST_CASE("size and clear")
{
    Cache c;
    CHECK(c.size() == 0);
    c.set("a.b", make_payload(0x01), "p", 1, 0);
    c.set("c.d", make_payload(0x02), "p", 2, 0);
    CHECK(c.size() == 2);
    c.clear();
    CHECK(c.size() == 0);
    CHECK(c.get("a.b") == nullptr);
}
