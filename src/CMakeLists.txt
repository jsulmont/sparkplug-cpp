# src/CMakeLists.txt
add_library(sparkplug_cpp
    payload_builder.cpp
    edge_node.cpp
    topic.cpp
    host_application.cpp
)

# Enable PIC for linking into shared libraries
set_target_properties(sparkplug_cpp PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(sparkplug_cpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(BUILD_STATIC_BUNDLE)
    target_link_libraries(sparkplug_cpp
        PUBLIC
            sparkplug_proto
            libprotobuf
            tl::expected
        PRIVATE
            paho-mqtt3as-static
    )
else()
    target_link_libraries(sparkplug_cpp
        PUBLIC
            sparkplug_proto
            protobuf::libprotobuf
            tl::expected
        PRIVATE
            eclipse-paho-mqtt-c::paho-mqtt3as
    )
endif()

if(NOT BUILD_STATIC_BUNDLE)
    add_library(sparkplug_c SHARED
        c_bindings.cpp
    )

    target_include_directories(sparkplug_c
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )

    target_link_libraries(sparkplug_c PRIVATE sparkplug_cpp)
endif()

if(BUILD_STATIC_BUNDLE)
    install(TARGETS sparkplug_cpp sparkplug_proto
        EXPORT SparkplugTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
else()
    install(TARGETS sparkplug_cpp sparkplug_c sparkplug_proto
        EXPORT SparkplugTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
    DESTINATION include
)

if(BUILD_STATIC_BUNDLE)
    create_static_bundle()
endif()