#pragma once

#include <filesystem>
#include <functional>
#include <unistd.h>
#include <msgpack.hpp>

#include "bacaro.h"
#include "internal.h"

// ── Pack helpers ──────────────────────────────────────────────────────────────

static Frame pack_float(double v)
{
    msgpack::sbuffer b; msgpack::pack(b, v);
    return Frame(b.data(), b.data() + b.size());
}

static Frame pack_int(int64_t v)
{
    msgpack::sbuffer b; msgpack::pack(b, v);
    return Frame(b.data(), b.data() + b.size());
}

static Frame pack_str(const std::string &s)
{
    msgpack::sbuffer b; msgpack::pack(b, s);
    return Frame(b.data(), b.data() + b.size());
}

// ── Unpack helpers ────────────────────────────────────────────────────────────

static double unpack_float(const uint8_t *data, size_t len)
{
    return msgpack::unpack(reinterpret_cast<const char *>(data), len).get().as<double>();
}

static int64_t unpack_int(const uint8_t *data, size_t len)
{
    return msgpack::unpack(reinterpret_cast<const char *>(data), len).get().as<int64_t>();
}

// ── Dispatch loop ─────────────────────────────────────────────────────────────

// Pump both processes until cond() is true or tries are exhausted.
static bool pump(bacaro_t *a, bacaro_t *b, std::function<bool()> cond, int tries = 100)
{
    for (int i = 0; i < tries && !cond(); ++i) {
        bacaro_dispatch(a);
        bacaro_dispatch(b);
        usleep(3000);
    }
    return cond();
}

// ── Test fixture ──────────────────────────────────────────────────────────────

// Isolates the runtime directory for a single test file.
struct Fixture {
    const char *dir;
    explicit Fixture(const char *d) : dir(d)
    {
        setenv("BACARO_RUNTIME_DIR", d, 1);
        std::filesystem::create_directories(d);
    }
    ~Fixture()
    {
        unsetenv("BACARO_RUNTIME_DIR");
        std::filesystem::remove_all(dir);
    }
};
