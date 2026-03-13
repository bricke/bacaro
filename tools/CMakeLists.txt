add_executable(oste oste.cpp)
target_link_libraries(oste PRIVATE bacaro msgpack-cxx)
target_include_directories(oste PRIVATE ${ZMQ_INCLUDE_DIRS})

add_executable(vecio vecio.cpp)
target_link_libraries(vecio PRIVATE bacaro msgpack-cxx)
target_include_directories(vecio PRIVATE ${ZMQ_INCLUDE_DIRS})

install(TARGETS oste vecio RUNTIME DESTINATION bin)

# convenience aliases
install(CODE "
    file(CREATE_LINK oste
         \"\${CMAKE_INSTALL_PREFIX}/bin/bacaro-monitor\" SYMBOLIC)
    file(CREATE_LINK vecio
         \"\${CMAKE_INSTALL_PREFIX}/bin/bacaro-set\" SYMBOLIC)
")
