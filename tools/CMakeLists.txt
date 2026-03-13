add_executable(oste oste.cpp)

target_link_libraries(oste PRIVATE bacaro msgpack-cxx)
target_include_directories(oste PRIVATE ${ZMQ_INCLUDE_DIRS})

install(TARGETS oste RUNTIME DESTINATION bin)

# bacaro-monitor is a convenience alias for oste
install(CODE "
    file(CREATE_LINK oste
         \"\${CMAKE_INSTALL_PREFIX}/bin/bacaro-monitor\"
         SYMBOLIC)
")
