function(add_bacaro_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE bacaro ${ZMQ_LIBRARIES} msgpack-cxx doctest::doctest)
    target_include_directories(${name} PRIVATE
        ${ZMQ_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/src
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_bacaro_test(test_wire)
add_bacaro_test(test_cache)
add_bacaro_test(test_lifecycle)
add_bacaro_test(test_discovery)
add_bacaro_test(test_snapshot)
add_bacaro_test(test_pubsub)
add_bacaro_test(test_get_domain)
