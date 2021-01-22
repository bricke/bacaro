#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <cstdlib>

#include "bacaro.h"

namespace fs = std::filesystem;

static std::string runtime_dir()
{
    const char *env = std::getenv("BACARO_RUNTIME_DIR");
    return env ? std::string(env) : "/tmp/bacaro";
}

// Count files in runtime_dir matching name prefix and given suffix
static int count_ipc_files(const std::string &prefix, const std::string &suffix)
{
    int count = 0;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(runtime_dir(), ec)) {
        std::string fname = entry.path().filename().string();
        if (fname.rfind(prefix, 0) == 0 && fname.size() > suffix.size()
            && fname.substr(fname.size() - suffix.size()) == suffix)
            ++count;
    }
    return count;
}

TEST_CASE("bacaro_new creates IPC files")
{
    bacaro_t *n = bacaro_new("testproc");
    REQUIRE(n != nullptr);

    CHECK(count_ipc_files("testproc.", ".pub") == 1);
    CHECK(count_ipc_files("testproc.", ".rep") == 1);

    bacaro_destroy(&n);
}

TEST_CASE("bacaro_destroy removes IPC files and nulls pointer")
{
    bacaro_t *n = bacaro_new("testproc");
    REQUIRE(n != nullptr);

    bacaro_destroy(&n);

    CHECK(n == nullptr);
    CHECK(count_ipc_files("testproc.", ".pub") == 0);
    CHECK(count_ipc_files("testproc.", ".rep") == 0);
}

TEST_CASE("bacaro_destroy is safe to call on null")
{
    bacaro_t *n = nullptr;
    CHECK_NOTHROW(bacaro_destroy(&n));
}

TEST_CASE("bacaro_new rejects null or empty name")
{
    CHECK(bacaro_new(nullptr) == nullptr);
    CHECK(bacaro_new("")      == nullptr);
}

TEST_CASE("two instances with different names coexist")
{
    bacaro_t *a = bacaro_new("proc_a");
    bacaro_t *b = bacaro_new("proc_b");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    CHECK(count_ipc_files("proc_a.", ".pub") == 1);
    CHECK(count_ipc_files("proc_b.", ".pub") == 1);

    bacaro_destroy(&a);
    bacaro_destroy(&b);

    CHECK(count_ipc_files("proc_a.", ".pub") == 0);
    CHECK(count_ipc_files("proc_b.", ".pub") == 0);
}

TEST_CASE("BACARO_RUNTIME_DIR env variable is respected")
{
    setenv("BACARO_RUNTIME_DIR", "/tmp/bacaro_test_custom", 1);

    bacaro_t *n = bacaro_new("envtest");
    REQUIRE(n != nullptr);

    CHECK(fs::exists("/tmp/bacaro_test_custom"));

    bacaro_destroy(&n);

    unsetenv("BACARO_RUNTIME_DIR");
    fs::remove_all("/tmp/bacaro_test_custom");
}
