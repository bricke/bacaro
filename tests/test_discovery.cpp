#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <unistd.h>

#include "bacaro.h"
#include "internal.h"
#include "test_helpers.h"

// Use an isolated runtime dir so tests don't interfere with each other
static const char *TEST_DIR = "/tmp/bacaro_test_discovery";

TEST_CASE("single instance has no peers on startup")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    CHECK(a->peers.empty());

    bacaro_destroy(&a);
}

TEST_CASE("second instance discovers first")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    bacaro_t *b = bacaro_new("beta", nullptr);
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
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    int fd = bacaro_fd(a);
    CHECK(fd >= 0);

    bacaro_destroy(&a);
}

TEST_CASE("peer disconnect removes peer but preserves cache")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    bacaro_t *b = bacaro_new("beta", nullptr);
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
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    // Subscribe before beta joins
    bacaro_subscribe(a, "sensors");
    CHECK(a->subscriptions.size() == 1);

    bacaro_t *b = bacaro_new("beta", nullptr);
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

TEST_CASE("manifest: no-manifest peer gets DEALER for any subscription")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr); // no manifest
    REQUIRE(a != nullptr);

    bacaro_t *b = bacaro_new("beta", nullptr);
    REQUIRE(b != nullptr);
    bacaro_subscribe(b, "sensors");

    usleep(10000);
    bacaro_dispatch(b);

    REQUIRE(b->peers.size() == 1);
    auto &peer = b->peers.begin()->second;
    CHECK_FALSE(peer.has_manifest);
    CHECK(peer.dealer_sock != nullptr); // DEALER created (no manifest = assume overlap)

    bacaro_destroy(&a);
    bacaro_destroy(&b);
}

TEST_CASE("manifest: overlapping subscription creates DEALER")
{
    const char *domains[] = {"sensors", "power", nullptr};
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", domains);
    REQUIRE(a != nullptr);

    bacaro_t *b = bacaro_new("beta", nullptr);
    REQUIRE(b != nullptr);
    bacaro_subscribe(b, "sensors");

    usleep(10000);
    bacaro_dispatch(b);

    REQUIRE(b->peers.size() == 1);
    auto &peer = b->peers.begin()->second;
    CHECK(peer.has_manifest);
    CHECK(peer.dealer_sock != nullptr); // overlap with "sensors"

    bacaro_destroy(&a);
    bacaro_destroy(&b);
}

TEST_CASE("manifest: non-overlapping subscription skips DEALER")
{
    const char *domains[] = {"sensors", "power", nullptr};
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", domains);
    REQUIRE(a != nullptr);

    bacaro_t *b = bacaro_new("beta", nullptr);
    REQUIRE(b != nullptr);
    bacaro_subscribe(b, "network");

    usleep(10000);
    bacaro_dispatch(b);

    REQUIRE(b->peers.size() == 1);
    auto &peer = b->peers.begin()->second;
    CHECK(peer.has_manifest);
    CHECK(peer.dealer_sock == nullptr); // no overlap with "network"

    bacaro_destroy(&a);
    bacaro_destroy(&b);
}

TEST_CASE("manifest: hierarchical domain overlap")
{
    const char *domains[] = {"sensors.temperature", nullptr};
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", domains);
    REQUIRE(a != nullptr);

    bacaro_t *b = bacaro_new("beta", nullptr);
    REQUIRE(b != nullptr);

    // Subscribe to parent domain — should overlap with child
    bacaro_subscribe(b, "sensors");

    usleep(10000);
    bacaro_dispatch(b);

    REQUIRE(b->peers.size() == 1);
    auto &peer = b->peers.begin()->second;
    CHECK(peer.has_manifest);
    CHECK(peer.dealer_sock != nullptr); // "sensors" is parent of "sensors.temperature"

    bacaro_destroy(&a);
    bacaro_destroy(&b);
}

TEST_CASE("manifest_overlaps helper")
{
    PeerInfo peer;

    // No manifest — always overlaps
    peer.has_manifest = false;
    CHECK(manifest_overlaps(peer, "anything"));
    CHECK(manifest_overlaps(peer, ""));

    // With manifest
    peer.has_manifest = true;
    peer.manifest = {"sensors", "power.battery"};

    CHECK(manifest_overlaps(peer, "sensors"));           // exact match
    CHECK(manifest_overlaps(peer, "sensors.temp"));      // child of declared domain
    CHECK(manifest_overlaps(peer, "power.battery"));     // exact match
    CHECK(manifest_overlaps(peer, "power"));             // parent of declared domain
    CHECK(manifest_overlaps(peer, ""));                  // empty prefix = subscribe_all
    CHECK_FALSE(manifest_overlaps(peer, "network"));     // no overlap
    CHECK_FALSE(manifest_overlaps(peer, "sensor"));      // not a prefix boundary
}

TEST_CASE("unsubscribe removes subscription")
{
    Fixture f(TEST_DIR);
    bacaro_t *a = bacaro_new("alpha", nullptr);
    REQUIRE(a != nullptr);

    bacaro_subscribe(a, "sensors");
    bacaro_subscribe(a, "network");
    CHECK(a->subscriptions.size() == 2);

    bacaro_unsubscribe(a, "sensors");
    CHECK(a->subscriptions.size() == 1);
    CHECK(a->subscriptions[0] == "network");

    bacaro_destroy(&a);
}
