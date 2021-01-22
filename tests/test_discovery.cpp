#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <unistd.h>

#include "bacaro.h"
#include "internal.h"

namespace fs = std::filesystem;

// Use an isolated runtime dir so tests don't interfere with each other
static const char *TEST_DIR = "/tmp/bacaro_test_discovery";

struct Fixture {
    Fixture()  { setenv("BACARO_RUNTIME_DIR", TEST_DIR, 1); fs::create_directories(TEST_DIR); }
    ~Fixture() { unsetenv("BACARO_RUNTIME_DIR"); fs::remove_all(TEST_DIR); }
};

TEST_CASE("single instance has no peers on startup")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    REQUIRE(a != nullptr);

    CHECK(a->peers.empty());

    bacaro_destroy(&a);
}

TEST_CASE("second instance discovers first")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    REQUIRE(a != nullptr);

    bacaro_t *b = bacaro_new("beta");
    REQUIRE(b != nullptr);

    // beta scanned runtime_dir at init and should have found alpha's .pub
    CHECK(b->peers.size() == 1);

    // alpha discovers beta via inotify — dispatch to process the event
    usleep(10000); // give inotify a moment
    bacaro_dispatch(a);
    CHECK(a->peers.size() == 1);

    bacaro_destroy(&a);
    bacaro_destroy(&b);
}

TEST_CASE("bacaro_fd returns a valid epoll fd")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    REQUIRE(a != nullptr);

    int fd = bacaro_fd(a);
    CHECK(fd >= 0);

    bacaro_destroy(&a);
}

TEST_CASE("peer disconnect removes peer but preserves cache")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    bacaro_t *b = bacaro_new("beta");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    usleep(10000);
    bacaro_dispatch(a); // let alpha discover beta

    REQUIRE(a->peers.size() == 1);

    // Seed alpha's cache with a fake entry from beta
    a->cache.set("sensors.temp", {0x01}, "beta", 1, 1000);
    CHECK(a->cache.get("sensors.temp") != nullptr);

    // Destroy beta — its .pub file is removed, inotify fires IN_DELETE
    bacaro_destroy(&b);

    usleep(10000);
    bacaro_dispatch(a);

    // Peer gone
    CHECK(a->peers.empty());

    // Cache entry survives
    CHECK(a->cache.get("sensors.temp") != nullptr);

    bacaro_destroy(&a);
}

TEST_CASE("subscribe applies filter to existing and new peers")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    REQUIRE(a != nullptr);

    // Subscribe before beta joins
    bacaro_subscribe(a, "sensors");
    CHECK(a->subscriptions.size() == 1);

    bacaro_t *b = bacaro_new("beta");
    REQUIRE(b != nullptr);

    usleep(10000);
    bacaro_dispatch(a);

    // alpha should have beta as a peer
    REQUIRE(a->peers.size() == 1);

    // Now subscribe to a new domain while peer is already connected
    bacaro_subscribe(a, "network");
    CHECK(a->subscriptions.size() == 2);

    // subscribe_all uses empty prefix
    bacaro_subscribe_all(a);
    CHECK(a->subscriptions.size() == 3);

    bacaro_destroy(&a);
    bacaro_destroy(&b);
}

TEST_CASE("unsubscribe removes subscription")
{
    Fixture f;
    bacaro_t *a = bacaro_new("alpha");
    REQUIRE(a != nullptr);

    bacaro_subscribe(a, "sensors");
    bacaro_subscribe(a, "network");
    CHECK(a->subscriptions.size() == 2);

    bacaro_unsubscribe(a, "sensors");
    CHECK(a->subscriptions.size() == 1);
    CHECK(a->subscriptions[0] == "network");

    bacaro_destroy(&a);
}
