#include <bacaro.h>
#include <msgpack.hpp>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <poll.h>
#include <sstream>
#include <string>

static std::atomic<bool> g_running{true};

static void on_signal(int) { g_running = false; }

static std::string msgpack_to_string(const uint8_t *data, size_t len)
{
    try {
        auto oh = msgpack::unpack(reinterpret_cast<const char *>(data), len);
        std::ostringstream oss;
        oss << oh.get();
        return oss.str();
    } catch (...) {
        // fallback: raw hex
        std::string hex = "0x";
        char buf[3];
        for (size_t i = 0; i < len; ++i) {
            snprintf(buf, sizeof(buf), "%02x", data[i]);
            hex += buf;
        }
        return hex;
    }
}

static void on_update(bacaro_t *self, const char *path,
                      const uint8_t *data, size_t len, void *)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *t = localtime(&ts.tv_sec);
    char timebuf[16];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", t);

    const char *publisher = bacaro_get_publisher(self, path);
    std::string value     = msgpack_to_string(data, len);

    printf("[%s.%03ld] %-40s = %s  (from: %s)\n",
           timebuf, ts.tv_nsec / 1000000L,
           path, value.c_str(),
           publisher ? publisher : "?");
    fflush(stdout);
}

int main()
{
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    bacaro_t *b = bacaro_new("oste", nullptr);
    if (!b) {
        fprintf(stderr, "oste: failed to initialise bacaro\n");
        return 1;
    }

    bacaro_subscribe_all(b);
    bacaro_on_update(b, on_update, nullptr);

    fprintf(stderr, "oste: listening on all properties — Ctrl+C to stop\n");

    int fd = bacaro_fd(b);
    while (g_running) {
        struct pollfd pfd = { fd, POLLIN, 0 };
        poll(&pfd, 1, 100);   // 100 ms timeout so we can check g_running
        bacaro_dispatch(b);
    }

    bacaro_destroy(&b);
    return 0;
}
