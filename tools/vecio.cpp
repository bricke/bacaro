#include <bacaro.h>
#include <msgpack.hpp>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <string>

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [-w <ms>] <path> <value>\n"
        "\n"
        "Sets a Bacaro property to the given value and exits.\n"
        "\n"
        "Options:\n"
        "  -w <ms>   Wait <ms> milliseconds after startup before publishing.\n"
        "            Useful when subscribers (e.g. oste) need time to connect.\n"
        "\n"
        "Value type is inferred automatically:\n"
        "  true / false   -> bool\n"
        "  integer        -> int64\n"
        "  decimal        -> float64\n"
        "  anything else  -> string\n"
        "\n"
        "Examples:\n"
        "  %s sensors.cpu.temperature 71.3\n"
        "  %s -w 500 system.status running\n"
        "  %s system.reboot_count 5\n"
        "  %s system.maintenance true\n",
        prog, prog, prog, prog, prog);
}

static std::vector<uint8_t> pack_value(const char *str)
{
    msgpack::sbuffer buf;
    char *end;

    if (strcmp(str, "true") == 0) {
        msgpack::pack(buf, true);
    } else if (strcmp(str, "false") == 0) {
        msgpack::pack(buf, false);
    } else {
        errno = 0;
        long long iv = strtoll(str, &end, 10);
        if (*end == '\0' && errno == 0) {
            msgpack::pack(buf, iv);
        } else {
            errno = 0;
            double dv = strtod(str, &end);
            if (*end == '\0' && errno == 0)
                msgpack::pack(buf, dv);
            else
                msgpack::pack(buf, std::string(str));
        }
    }

    return std::vector<uint8_t>(buf.data(), buf.data() + buf.size());
}

int main(int argc, char **argv)
{
    int wait_ms = 0;
    int argi = 1;

    if (argi < argc && strcmp(argv[argi], "-w") == 0) {
        argi++;
        if (argi >= argc) {
            fprintf(stderr, "vecio: -w requires an argument\n");
            return 1;
        }
        char *end;
        errno = 0;
        long v = strtol(argv[argi], &end, 10);
        if (*end != '\0' || errno != 0 || v < 0) {
            fprintf(stderr, "vecio: -w value must be a non-negative integer\n");
            return 1;
        }
        wait_ms = (int)v;
        argi++;
    }

    if (argc - argi != 2) {
        usage(argv[0]);
        return 1;
    }

    const char *path  = argv[argi];
    const char *value = argv[argi + 1];

    bacaro_t *b = bacaro_new("vecio");
    if (!b) {
        fprintf(stderr, "vecio: failed to initialise bacaro\n");
        return 1;
    }

    if (wait_ms > 0) {
        int fd = bacaro_fd(b);
        struct pollfd pfd = { fd, POLLIN, 0 };
        poll(&pfd, 1, wait_ms);
        bacaro_dispatch(b);
    }

    auto payload = pack_value(value);
    int rc = bacaro_set(b, path, payload.data(), payload.size());
    if (rc != BACARO_OK) {
        fprintf(stderr, "vecio: bacaro_set failed (%d)\n", rc);
        bacaro_destroy(&b);
        return 1;
    }

    bacaro_destroy(&b);
    return 0;
}
