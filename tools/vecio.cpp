#include <bacaro.h>
#include <msgpack.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <path> <value>\n"
        "\n"
        "Sets a Bacaro property to the given value and exits.\n"
        "\n"
        "Value type is inferred automatically:\n"
        "  true / false   -> bool\n"
        "  integer        -> int64\n"
        "  decimal        -> float64\n"
        "  anything else  -> string\n"
        "\n"
        "Examples:\n"
        "  %s sensors.cpu.temperature 71.3\n"
        "  %s system.status running\n"
        "  %s system.reboot_count 5\n"
        "  %s system.maintenance true\n",
        prog, prog, prog, prog, prog);
}

static std::vector<uint8_t> pack_value(const char *str)
{
    msgpack::sbuffer buf;
    char *end;

    if (strcmp(str, "true") == 0)
        msgpack::pack(buf, true);
    else if (strcmp(str, "false") == 0)
        msgpack::pack(buf, false);
    else if (long long iv = strtoll(str, &end, 10); *end == '\0')
        msgpack::pack(buf, iv);
    else if (double dv = strtod(str, &end); *end == '\0')
        msgpack::pack(buf, dv);
    else
        msgpack::pack(buf, std::string(str));

    return std::vector<uint8_t>(buf.data(), buf.data() + buf.size());
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        usage(argv[0]);
        return 1;
    }

    const char *path  = argv[1];
    const char *value = argv[2];

    bacaro_t *b = bacaro_new("vecio");
    if (!b) {
        fprintf(stderr, "vecio: failed to initialise bacaro\n");
        return 1;
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
