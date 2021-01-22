#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <unistd.h>
#include <set>
#include <msgpack.hpp>

#include "bacaro.h"
#include "internal.h"

static const char *TEST_DIR = "/tmp/bacaro_test_get_domain";

struct Fixture {
    Fixture()  { setenv("BACARO_RUNTIME_DIR", TEST_DIR, 1);
                 std::filesystem::create_directories(TEST_DIR); }
    ~Fixture() { unsetenv("BACARO_RUNTIME_DIR");
                 std::filesystem::remove_all(TEST_DIR); }
};

static Frame pack_float(double v)
{
    msgpack::sbuffer b; msgpack::pack(b, v);
    return Frame(b.data(), b.data() + b.size());
}

static double unpack_float(const uint8_t *data, size_t len)
{
    return msgpack::unpack(reinterpret_cast<const char *>(data), len).get().as<double>();
}

// Collect all paths from a proplist into a set for order-independent checks
static std::set<std::string> proplist_paths(const bacaro_proplist_t *list)
{
    std::set<std::string> paths;
    for (size_t i = 0; i < bacaro_proplist_size(list); ++i)
        paths.insert(bacaro_proplist_path(list, i));
    return paths;
}

// ── null / invalid arg handling ──────────────────────────────────────────────

TEST_CASE("bacaro_get_domain rejects null arguments")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    REQUIRE(a != nullptr);

    CHECK(bacaro_get_domain(nullptr, "sensors") == nullptr);
    CHECK(bacaro_get_domain(a, nullptr)         == nullptr);

    bacaro_destroy(&a);
}

TEST_CASE("bacaro_proplist_destroy is safe on null")
{
    bacaro_proplist_t *list = nullptr;
    CHECK_NOTHROW(bacaro_proplist_destroy(&list));
    CHECK_NOTHROW(bacaro_proplist_destroy(nullptr));
}

TEST_CASE("proplist accessors return safe defaults on out-of-bounds index")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    REQUIRE(a != nullptr);

    bacaro_proplist_t *list = bacaro_get_domain(a, "sensors");
    REQUIRE(list != nullptr);
    REQUIRE(bacaro_proplist_size(list) == 0);

    // Index 0 on empty list
    size_t len;
    CHECK(bacaro_proplist_path(list, 0)          == nullptr);
    CHECK(bacaro_proplist_value(list, 0, &len)    == nullptr);
    CHECK(bacaro_proplist_publisher(list, 0)      == nullptr);
    CHECK(bacaro_proplist_sequence(list, 0)       == 0);
    CHECK(bacaro_proplist_timestamp(list, 0)      == 0);

    // nullptr list
    CHECK(bacaro_proplist_path(nullptr, 0)        == nullptr);
    CHECK(bacaro_proplist_value(nullptr, 0, &len) == nullptr);
    CHECK(bacaro_proplist_publisher(nullptr, 0)   == nullptr);
    CHECK(bacaro_proplist_size(nullptr)           == 0);

    bacaro_proplist_destroy(&list);
    bacaro_destroy(&a);
}

// ── empty domain ─────────────────────────────────────────────────────────────

TEST_CASE("bacaro_get_domain returns empty list for unknown domain")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    REQUIRE(a != nullptr);

    bacaro_proplist_t *list = bacaro_get_domain(a, "bluetooth");
    REQUIRE(list != nullptr);
    CHECK(bacaro_proplist_size(list) == 0);

    bacaro_proplist_destroy(&list);
    CHECK(list == nullptr);
    bacaro_destroy(&a);
}

// ── basic content ─────────────────────────────────────────────────────────────

TEST_CASE("bacaro_get_domain returns all matching properties")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    REQUIRE(a != nullptr);

    Frame v = pack_float(1.0);
    bacaro_set(a, "sensors.cpu.temp",  v.data(), v.size());
    bacaro_set(a, "sensors.cpu.load",  v.data(), v.size());
    bacaro_set(a, "sensors.gpu.temp",  v.data(), v.size());
    bacaro_set(a, "network.eth0.rx",   v.data(), v.size()); // different domain

    SUBCASE("prefix sensors returns 3 entries")
    {
        bacaro_proplist_t *list = bacaro_get_domain(a, "sensors");
        REQUIRE(list != nullptr);
        CHECK(bacaro_proplist_size(list) == 3);

        auto paths = proplist_paths(list);
        CHECK(paths.count("sensors.cpu.temp") == 1);
        CHECK(paths.count("sensors.cpu.load") == 1);
        CHECK(paths.count("sensors.gpu.temp") == 1);
        CHECK(paths.count("network.eth0.rx")  == 0);

        bacaro_proplist_destroy(&list);
    }

    SUBCASE("prefix sensors.cpu returns 2 entries")
    {
        bacaro_proplist_t *list = bacaro_get_domain(a, "sensors.cpu");
        REQUIRE(list != nullptr);
        CHECK(bacaro_proplist_size(list) == 2);

        auto paths = proplist_paths(list);
        CHECK(paths.count("sensors.cpu.temp") == 1);
        CHECK(paths.count("sensors.cpu.load") == 1);

        bacaro_proplist_destroy(&list);
    }

    SUBCASE("empty prefix returns all 4 entries")
    {
        bacaro_proplist_t *list = bacaro_get_domain(a, "");
        REQUIRE(list != nullptr);
        CHECK(bacaro_proplist_size(list) == 4);
        bacaro_proplist_destroy(&list);
    }

    bacaro_destroy(&a);
}

TEST_CASE("bacaro_proplist_value returns correct data")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    REQUIRE(a != nullptr);

    Frame v = pack_float(72.5);
    bacaro_set(a, "sensors.temp", v.data(), v.size());

    bacaro_proplist_t *list = bacaro_get_domain(a, "sensors");
    REQUIRE(list != nullptr);
    REQUIRE(bacaro_proplist_size(list) == 1);

    size_t len;
    const uint8_t *data = bacaro_proplist_value(list, 0, &len);
    REQUIRE(data != nullptr);
    CHECK(unpack_float(data, len) == doctest::Approx(72.5));

    bacaro_proplist_destroy(&list);
    bacaro_destroy(&a);
}

TEST_CASE("bacaro_proplist_publisher returns correct publisher")
{
    Fixture f;
    bacaro_t *a = bacaro_new("powerd");
    REQUIRE(a != nullptr);

    Frame v = pack_float(1.0);
    bacaro_set(a, "system.battery", v.data(), v.size());

    bacaro_proplist_t *list = bacaro_get_domain(a, "system");
    REQUIRE(list != nullptr);
    REQUIRE(bacaro_proplist_size(list) == 1);

    CHECK(std::string(bacaro_proplist_publisher(list, 0)) == "powerd");

    bacaro_proplist_destroy(&list);
    bacaro_destroy(&a);
}

TEST_CASE("bacaro_proplist_sequence and timestamp are populated")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    REQUIRE(a != nullptr);

    Frame v = pack_float(1.0);
    bacaro_set(a, "sensors.a", v.data(), v.size()); // seq=1
    bacaro_set(a, "sensors.b", v.data(), v.size()); // seq=2

    bacaro_proplist_t *list = bacaro_get_domain(a, "sensors");
    REQUIRE(list != nullptr);
    REQUIRE(bacaro_proplist_size(list) == 2);

    // Collect sequences — order not guaranteed
    std::set<uint64_t> seqs;
    for (size_t i = 0; i < bacaro_proplist_size(list); ++i) {
        seqs.insert(bacaro_proplist_sequence(list, i));
        CHECK(bacaro_proplist_timestamp(list, i) > 0);
    }
    CHECK(seqs.count(1) == 1);
    CHECK(seqs.count(2) == 1);

    bacaro_proplist_destroy(&list);
    bacaro_destroy(&a);
}

TEST_CASE("bacaro_get_domain does not match partial segment names")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    REQUIRE(a != nullptr);

    Frame v = pack_float(1.0);
    bacaro_set(a, "sensor.value",  v.data(), v.size());
    bacaro_set(a, "sensors.value", v.data(), v.size());

    // "sensor" must not match "sensors.value"
    bacaro_proplist_t *list = bacaro_get_domain(a, "sensor");
    REQUIRE(list != nullptr);
    CHECK(bacaro_proplist_size(list) == 1);
    CHECK(std::string(bacaro_proplist_path(list, 0)) == "sensor.value");

    bacaro_proplist_destroy(&list);
    bacaro_destroy(&a);
}

// ── integration: populated via pub/sub ───────────────────────────────────────

TEST_CASE("bacaro_get_domain returns properties received from remote publisher")
{
    Fixture f;

    bacaro_t *pub = bacaro_new("publisher");
    bacaro_t *sub = bacaro_new("subscriber");
    REQUIRE(pub != nullptr);
    REQUIRE(sub != nullptr);

    bacaro_subscribe(sub, "sensors");
    usleep(20000);
    bacaro_dispatch(pub);
    bacaro_dispatch(sub);

    Frame v1 = pack_float(55.0);
    Frame v2 = pack_float(80.0);
    bacaro_set(pub, "sensors.cpu.temp", v1.data(), v1.size());
    bacaro_set(pub, "sensors.gpu.temp", v2.data(), v2.size());

    // Pump until both arrive
    for (int i = 0; i < 100; ++i) {
        bacaro_dispatch(pub);
        bacaro_dispatch(sub);
        if (sub->cache.get("sensors.cpu.temp") && sub->cache.get("sensors.gpu.temp"))
            break;
        usleep(3000);
    }

    REQUIRE(sub->cache.get("sensors.cpu.temp") != nullptr);
    REQUIRE(sub->cache.get("sensors.gpu.temp") != nullptr);

    bacaro_proplist_t *list = bacaro_get_domain(sub, "sensors");
    REQUIRE(list != nullptr);
    CHECK(bacaro_proplist_size(list) == 2);

    auto paths = proplist_paths(list);
    CHECK(paths.count("sensors.cpu.temp") == 1);
    CHECK(paths.count("sensors.gpu.temp") == 1);

    for (size_t i = 0; i < bacaro_proplist_size(list); ++i)
        CHECK(std::string(bacaro_proplist_publisher(list, i)) == "publisher");

    bacaro_proplist_destroy(&list);
    bacaro_destroy(&pub);
    bacaro_destroy(&sub);
}
