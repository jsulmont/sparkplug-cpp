# cmake/StaticBundle.cmake

option(BUILD_STATIC_BUNDLE "Build self-contained static library bundle" OFF)

if(NOT BUILD_STATIC_BUNDLE)
    return()
endif()

message(STATUS "Building static bundle - fetching dependencies from source")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

FetchContent_Declare(
    paho-mqtt-c
    GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.c.git
    GIT_TAG v1.3.15
    GIT_SHALLOW TRUE
)
set(PAHO_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(PAHO_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PAHO_WITH_SSL ON CACHE BOOL "" FORCE)
set(PAHO_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(PAHO_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(paho-mqtt-c)

FetchContent_Declare(
    abseil-cpp
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG 20240722.0
    GIT_SHALLOW TRUE
)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ABSL_USE_GOOGLETEST_HEAD OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(abseil-cpp)

FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG v29.4
    GIT_SHALLOW TRUE
)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(protobuf)

# Note: protoc target is now available as 'protoc' (not protobuf::protoc in this version)
# The proto/CMakeLists.txt will use this target
include(${protobuf_SOURCE_DIR}/cmake/protobuf-generate.cmake)

function(create_static_bundle)
    add_library(sparkplug_bundle_objects OBJECT
        ${CMAKE_CURRENT_SOURCE_DIR}/payload_builder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/edge_node.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/topic.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/host_application.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/c_bindings.cpp
    )

    target_include_directories(sparkplug_bundle_objects
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )

    target_link_libraries(sparkplug_bundle_objects
        PUBLIC
            sparkplug_proto
            libprotobuf
            tl::expected
        PRIVATE
            paho-mqtt3as-static
    )

    add_library(sparkplug_c_bundle STATIC
        $<TARGET_OBJECTS:sparkplug_bundle_objects>
        $<TARGET_OBJECTS:sparkplug_proto>
    )

    set_target_properties(sparkplug_c_bundle PROPERTIES
        OUTPUT_NAME "sparkplug_c_bundle"
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
    )

    target_link_libraries(sparkplug_c_bundle PUBLIC
        paho-mqtt3as-static
        libprotobuf
        OpenSSL::SSL
        OpenSSL::Crypto
    )

    install(TARGETS sparkplug_c_bundle
        EXPORT SparkplugBundleTargets
        ARCHIVE DESTINATION lib
    )

    message(STATUS "Static bundle created: sparkplug_c_bundle (bundles Paho, Protobuf, Abseil)")
endfunction()
