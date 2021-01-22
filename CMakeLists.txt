cmake_minimum_required(VERSION 3.16)
project(bacaro VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

include(FetchContent)

FetchContent_Declare(
    msgpack
    GIT_REPOSITORY https://github.com/msgpack/msgpack-c.git
    GIT_TAG        cpp-6.1.1
)
set(MSGPACK_CXX17 ON CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MSGPACK_USE_BOOST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(msgpack)

# Library
add_library(bacaro
    src/bacaro.cpp
    src/wire.cpp
    src/cache.cpp
    src/discovery.cpp
)

target_include_directories(bacaro
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${ZMQ_INCLUDE_DIRS}
        src
)

target_link_libraries(bacaro
    PRIVATE
        ${ZMQ_LIBRARIES}
        msgpack-cxx
)

target_compile_options(bacaro PRIVATE ${ZMQ_CFLAGS_OTHER})

# Tests — disabled by setting BACARO_BUILD_TESTS=OFF (CMake option or env variable)
option(BACARO_BUILD_TESTS "Build Bacaro tests" ON)
if(DEFINED ENV{BACARO_BUILD_TESTS})
    set(BACARO_BUILD_TESTS $ENV{BACARO_BUILD_TESTS} CACHE BOOL "" FORCE)
endif()

if(BACARO_BUILD_TESTS)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
    )
    FetchContent_MakeAvailable(doctest)
    enable_testing()
    add_subdirectory(tests)
endif()
